#include "../helpers/ProgramUtilities.h"
#include "../helpers/ResourcesManager.h"
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>
#include <iostream>
#include <nfd.h>
#include <stdio.h>
#include <vector>

#include "LiveRenderer.h"
#include <algorithm>
#include <fstream>



SystemAction::SystemAction(SystemAction::Type act) {
	type = act;
	data = glm::ivec4(0);
}

LiveRenderer::LiveRenderer(int winW, int winH, bool fullscreen) {
	_showGUI = false;
	_showDebug = false;

	_fullscreen = fullscreen;
	_windowSize = glm::ivec2(winW, winH);

	// GL options
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glCullFace(GL_BACK);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendEquation(GL_FUNC_ADD);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	_camera.screen(winW, winH, 1.0f);
	// Setup framebuffers, size does not really matter as we expect.
	const glm::ivec2 renderSize = _camera.renderSize();
	_particlesFramebuffer = std::shared_ptr<Framebuffer>(new Framebuffer(renderSize[0], renderSize[1],
		GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, GL_CLAMP_TO_EDGE));
	_blurFramebuffer = std::shared_ptr<Framebuffer>(new Framebuffer(renderSize[0], renderSize[1],
		GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, GL_CLAMP_TO_EDGE));
	_renderFramebuffer = std::shared_ptr<Framebuffer>(new Framebuffer(renderSize[0], renderSize[1],
	GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, GL_CLAMP_TO_EDGE));
	_finalFramebuffer = std::shared_ptr<Framebuffer>(new Framebuffer(renderSize[0], renderSize[1],
		GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, GL_CLAMP_TO_EDGE));

	_backgroundTexture.init("backgroundtexture_frag", "backgroundtexture_vert");
	_blurringScreen.init(_particlesFramebuffer->textureId(), "particlesblur_frag");
	_fxaa.init("fxaa_frag");
	_passthrough.init("screenquad_frag");

	// Create the layers.
	//_layers[Layer::BGCOLOR].type = Layer::BGCOLOR;
	//_layers[Layer::BGCOLOR].name = "Background color";
	//_layers[Layer::BGCOLOR].toggle = &_state.showBackground;

	_layers[Layer::BGTEXTURE].type = Layer::BGTEXTURE;
	_layers[Layer::BGTEXTURE].name = "Background image";
	_layers[Layer::BGTEXTURE].draw = &LiveRenderer::drawBackgroundImage;

	_layers[Layer::BLUR].type = Layer::BLUR;
	_layers[Layer::BLUR].name = "Blur effect";
	_layers[Layer::BLUR].draw = &LiveRenderer::drawBlur;

	_layers[Layer::ANNOTATIONS].type = Layer::ANNOTATIONS;
	_layers[Layer::ANNOTATIONS].name = "Score";
	_layers[Layer::ANNOTATIONS].draw = &LiveRenderer::drawScore;

	_layers[Layer::KEYBOARD].type = Layer::KEYBOARD;
	_layers[Layer::KEYBOARD].name = "Keyboard";
	_layers[Layer::KEYBOARD].draw = &LiveRenderer::drawKeyboard;

	_layers[Layer::PARTICLES].type = Layer::PARTICLES;
	_layers[Layer::PARTICLES].name = "Particles";
	_layers[Layer::PARTICLES].draw = &LiveRenderer::drawParticles;

	_layers[Layer::NOTES].type = Layer::NOTES;
	_layers[Layer::NOTES].name = "Notes";
	_layers[Layer::NOTES].draw = &LiveRenderer::drawNotes;

	_layers[Layer::FLASHES].type = Layer::FLASHES;
	_layers[Layer::FLASHES].name = "Flashes";
	_layers[Layer::FLASHES].draw = &LiveRenderer::drawFlashes;

	_layers[Layer::PEDAL].type = Layer::PEDAL;
	_layers[Layer::PEDAL].name = "Pedal";
	_layers[Layer::PEDAL].draw = &LiveRenderer::drawPedals;

	_layers[Layer::WAVE].type = Layer::WAVE;
	_layers[Layer::WAVE].name = "Waves";
	_layers[Layer::WAVE].draw = &LiveRenderer::drawWaves;

	// Register state.
	_layers[Layer::BGTEXTURE].toggle = &_state.background.image;
	_layers[Layer::BLUR].toggle = &_state.showBlur;
	_layers[Layer::ANNOTATIONS].toggle = &_state.showScore;
	_layers[Layer::KEYBOARD].toggle = &_state.showKeyboard;
	_layers[Layer::PARTICLES].toggle = &_state.showParticles;
	_layers[Layer::NOTES].toggle = &_state.showNotes;
	_layers[Layer::FLASHES].toggle = &_state.showFlashes;
	_layers[Layer::PEDAL].toggle = &_state.showPedal;
	_layers[Layer::WAVE].toggle = &_state.showWave;

	// Check setup errors.
	checkGLError();

	_score.reset(new Score(2.0f, true));
	_scene.reset(new LiveMIDIScene());
}

LiveRenderer::~LiveRenderer() {}

bool LiveRenderer::loadFile(const std::string &midiFilePath) {
	return false;
}

SystemAction LiveRenderer::draw(float currentTime) {

	// -- Default mode --

	// Compute the time elapsed since last frame, or keep the same value if
	// playback is disabled.
	_timer = _shouldPlay ? (currentTime - _timerStart) : _timer;

	// Render scene and blit, with GUI on top if needed.
	drawScene(false);

	glViewport(0, 0, GLsizei(_camera.screenSize()[0]), GLsizei(_camera.screenSize()[1]));
	_passthrough.draw(_finalFramebuffer->textureId(), _timer);

	SystemAction action = SystemAction::NONE;
	if (_showGUI) {
		action = drawGUI(currentTime);
	}

	return action;
}

void LiveRenderer::drawScene(bool transparentBG){

	_scene->updateNotesBuffer(_timer);

	// Update active notes listing (for particles).
	_scene->updatesActiveNotes(_timer);

	const glm::vec2 invSizeFb = 1.0f / glm::vec2(_renderFramebuffer->_width, _renderFramebuffer->_height);

	// Blur rendering.
	if (_state.showBlur) {
		blurPrepass();
	}

	// Set viewport
	_renderFramebuffer->bind();
	glViewport(0, 0, _renderFramebuffer->_width, _renderFramebuffer->_height);

	// Final pass (directly on screen).
	// Background color.
	if(transparentBG){
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	} else {
		glClearColor(_state.background.color[0], _state.background.color[1], _state.background.color[2], 1.0f);
	}

	glClear(GL_COLOR_BUFFER_BIT);

	// Draw the layers in order.
	for (int i = 0; i < _state.layersMap.size(); ++i) {
		const int layerId = _state.layersMap[i];
		if (layerId >= _layers.size()) {
			continue;
		}
		if (_layers[layerId].draw && *(_layers[layerId].toggle)) {
			(this->*_layers[layerId].draw)(invSizeFb);
		}
	}

	_renderFramebuffer->unbind();

	// Apply fxaa.
	if(_state.applyAA){
		_finalFramebuffer->bind();
		_fxaa.draw(_renderFramebuffer->textureId(), 0.0, invSizeFb);
		_finalFramebuffer->unbind();
	} else {
		// Else just do a blit.
		_renderFramebuffer->bind(GL_READ_FRAMEBUFFER);
		_finalFramebuffer->bind(GL_DRAW_FRAMEBUFFER);
		glBlitFramebuffer(0, 0, _renderFramebuffer->_width, _renderFramebuffer->_height, 0, 0, _finalFramebuffer->_width, _finalFramebuffer->_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		_finalFramebuffer->unbind();
	}

}

void LiveRenderer::blurPrepass() {
	const glm::vec2 invSizeB = 1.0f / glm::vec2(_particlesFramebuffer->_width, _particlesFramebuffer->_height);
	// Bind particles buffer.
	_particlesFramebuffer->bind();
	// Set viewport.
	glViewport(0, 0, _particlesFramebuffer->_width, _particlesFramebuffer->_height);
	// Draw blurred particles from previous frames.
	_passthrough.draw(_blurFramebuffer->textureId(), _timer);
	if (_state.showParticles) {
		// Draw the new particles.
		_scene->drawParticles(_timer, invSizeB, _state.particles, true);
	}
	if (_state.showBlurNotes) {
		// Draw the notes.
		_scene->drawNotes(_timer, invSizeB, _state.baseColors, _state.minorColors, true);
	}

	_particlesFramebuffer->unbind();

	// Bind blur framebuffer.
	_blurFramebuffer->bind();
	glViewport(0, 0, _blurFramebuffer->_width, _blurFramebuffer->_height);
	// Perform box blur on result from particles pass.
	_blurringScreen.draw(_timer);
	_blurFramebuffer->unbind();

}

void LiveRenderer::drawBackgroundImage(const glm::vec2 &) {
	// Use background.tex and background.imageAlpha
	// Early exit if no texture or transparent.
	if(_state.background.tex == 0 || _state.background.imageAlpha < 1.0f/255.0f) {
		return;
	}
	glEnable(GL_BLEND);
	glUseProgram(_backgroundTexture.programId());
	glUniform1f(glGetUniformLocation(_backgroundTexture.programId(), "textureAlpha"), _state.background.imageAlpha);
	glUniform1i(glGetUniformLocation(_backgroundTexture.programId(), "behindKeyboard"), _state.background.imageBehindKeyboard);
	glUniform1f(glGetUniformLocation(_backgroundTexture.programId(), "keyboardHeight"), _state.keyboard.size);
	_backgroundTexture.draw(_state.background.tex, _timer);

	glDisable(GL_BLEND);
}

void LiveRenderer::drawBlur(const glm::vec2 &) {
	glEnable(GL_BLEND);
	_passthrough.draw(_blurFramebuffer->textureId(), _timer);
	glDisable(GL_BLEND);
}

void LiveRenderer::drawParticles(const glm::vec2 & invSize) {
	_scene->drawParticles(_timer, invSize, _state.particles, false);
}

void LiveRenderer::drawScore(const glm::vec2 & invSize) {
	_score->draw(_timer, invSize);
}

void LiveRenderer::drawKeyboard(const glm::vec2 & invSize) {
	const ColorArray & majColors = _state.keyboard.customKeyColors ? _state.keyboard.majorColor : _state.baseColors;
	const ColorArray & minColors = _state.keyboard.customKeyColors ? _state.keyboard.minorColor : _state.minorColors;
	_scene->drawKeyboard(_timer, invSize, _state.background.keysColor, majColors, minColors, _state.keyboard.highlightKeys);
}

void LiveRenderer::drawNotes(const glm::vec2 & invSize) {
	_scene->drawNotes(_timer, invSize, _state.baseColors, _state.minorColors, false);
}

void LiveRenderer::drawFlashes(const glm::vec2 & invSize) {
	_scene->drawFlashes(_timer, invSize, _state.flashColors, _state.flashSize);
}

void LiveRenderer::drawPedals(const glm::vec2 & invSize){
	// Extra shift above the waves.
	_scene->drawPedals(_timer, invSize, _state.pedals, _state.keyboard.size + (_state.showWave ? 0.01f : 0.0f));
}

void LiveRenderer::drawWaves(const glm::vec2 & invSize){
	_scene->drawWaves(_timer, invSize, _state.waves, _state.keyboard.size);
}

SystemAction LiveRenderer::drawGUI(const float currentTime) {

	SystemAction action = SystemAction::NONE;

	if (ImGui::Begin("Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

		action = showTopButtons(currentTime);
		ImGui::Separator();

		// Detail text.
		const int nCount = _scene->notesCount()/*_scene->midiFile().notesCount()*/;
		const double duration = _scene->duration();
		const int speed = int(std::round(double(nCount)/duration));
		ImGui::Text("Notes: %d, duration: %.1fs, speed: %d notes/s", nCount, duration, speed);
		ImGui::Separator();
		
		// Load button.
		if (ImGui::Button("Load MIDI file...")) {
			// Read arguments.
			nfdchar_t *outPath = NULL;
			nfdresult_t result = NFD_OpenDialog(NULL, NULL, &outPath);
			if (result == NFD_OKAY) {
				loadFile(std::string(outPath));
			}
		}
		ImGui::SameLine(COLUMN_SIZE);
		ImGui::PushItemWidth(100);
		if (ImGui::InputFloat("Preroll", &_state.prerollTime, 0.1f, 1.0f)) {
			reset();
		}
		ImGui::PopItemWidth();

		if (ImGui::Button("Show layers...")) {
			_showLayers = true;
		}
		ImGui::SameLine(COLUMN_SIZE);
		ImGui::PushItemWidth(100);
		if (ImGui::Combo("Quality", (int *)(&_state.quality), "Half\0Low\0Medium\0High\0Double\0\0")) {
			updateSizes();
		}

		if (ImGui::Checkbox("Sync effect colors", &_state.lockParticleColor)) {
			// If we enable the lock, make sure the colors are synched.
			synchronizeColors(_state.baseColors);
		}
		// Add FXAA.
		ImGui::SameLine(COLUMN_SIZE);
		ImGui::Checkbox("Smoothing", &_state.applyAA);

		if(ImGui::Combo("Min key", &_state.minKey, midiKeysString)){
			updateMinMaxKeys();
		}
		ImGui::SameLine(COLUMN_SIZE);
		if(ImGui::Combo("Max key", &_state.maxKey, midiKeysString)){
			updateMinMaxKeys();
		}



		if(ImGui::CollapsingHeader("Notes##HEADER")){

			bool smw0 = ImGui::InputFloat("Scale", &_state.scale, 0.01f, 0.1f);
			ImGui::SameLine(COLUMN_SIZE);
			smw0 = ImGui::SliderFloat("Minor size", &_state.background.minorsWidth, 0.1f, 1.0f, "%.2f") || smw0;
			if (smw0) {
				_state.scale = std::max(_state.scale, 0.01f);
				_state.background.minorsWidth = std::min(std::max(_state.background.minorsWidth, 0.1f), 1.0f);
				_scene->setScaleAndMinorWidth(_state.scale, _state.background.minorsWidth);
				_score->setScaleAndMinorWidth(_state.scale, _state.background.minorsWidth);
			}

			if(channelColorEdit("Notes", "Notes", _state.baseColors)){
				synchronizeColors(_state.baseColors);
			}
			ImGui::SameLine();
			if(channelColorEdit("Minors", "Minors", _state.minorColors)){
				synchronizeColors(_state.minorColors);
			}

			ImGui::SameLine(COLUMN_SIZE);

			if(ImGui::Checkbox("Per-set colors", &_state.perChannelColors)){
				if(!_state.perChannelColors){
					_state.synchronizeChannels();
				}
			}
			if(_state.perChannelColors){
				if(ImGui::Button("Define sets...")){
					ImGui::OpenPopup("Note sets options");
				}
				showSets();
			}
		}


		if (_state.showFlashes && ImGui::CollapsingHeader("Flashes##HEADER")) {

			if(channelColorEdit("Color##Flashes", "Color", _state.flashColors)){
				synchronizeColors(_state.flashColors);
			}

			ImGui::SameLine(COLUMN_SIZE);
			ImGui::PushItemWidth(100);
			ImGui::SliderFloat("Flash size", &_state.flashSize, 0.1f, 3.0f);
			ImGui::PopItemWidth();
		}


		if (_state.showParticles && ImGui::CollapsingHeader("Particles##HEADER")) {
			showParticleOptions();
		}

		if (_state.showKeyboard && ImGui::CollapsingHeader("Keyboard##HEADER")) {
			showKeyboardOptions();
		}

		if(_state.showPedal && ImGui::CollapsingHeader("Pedal##HEADER")){
			showPedalOptions();
		}

		if(_state.showWave && ImGui::CollapsingHeader("Wave##HEADER")){
			showWaveOptions();
		}

		if (_state.showScore && ImGui::CollapsingHeader("Score##HEADER")) {
			showScoreOptions();
		}

		if (_state.showBlur && ImGui::CollapsingHeader("Blur##HEADER")) {
			showBlurOptions();
		}

		if (ImGui::CollapsingHeader("Background##HEADER")) {
			showBackgroundOptions();
		}
		ImGui::Separator();

		showBottomButtons();

		if (_showDebug) {
			ImGui::Separator();
			ImGui::Text("Debug: ");
			ImGui::SameLine();
			ImGui::TextDisabled("(press D to hide)");
			ImGui::Text("%.1f FPS / %.1f ms", ImGui::GetIO().Framerate, ImGui::GetIO().DeltaTime * 1000.0f);
			ImGui::Text("Render size: %dx%d, screen size: %dx%d", _renderFramebuffer->_width, _renderFramebuffer->_height, _camera.screenSize()[0], _camera.screenSize()[1]);
			if (ImGui::Button("Print MIDI content to console")) {
				//_scene->midiFile().print();
			}
		}
	}
	ImGui::End();

	if (_showLayers) {
		showLayers();
	}

	return action;
}

void LiveRenderer::synchronizeColors(const ColorArray & colors){
	// Keep the colors in sync if needed.
	if (!_state.lockParticleColor) {
		return;
	}

	for(size_t cid = 0; cid < CHANNELS_COUNT; ++cid){
		_state.baseColors[cid] = _state.particles.colors[cid] = _state.minorColors[cid] = _state.flashColors[cid] = colors[cid];
	}

	// If we have only one channel, synchronize one-shoft effects.
	if(!_state.perChannelColors){
		_state.pedals.color = _state.waves.color = _state.baseColors[0];
	}
}

SystemAction LiveRenderer::showTopButtons(double currentTime){
	if (ImGui::Button(_shouldPlay ? "Pause (p)" : "Play (p)")) {
		_shouldPlay = !_shouldPlay;
		_timerStart = currentTime - _timer;
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart (r)")) {
		reset();
	}
	ImGui::SameLine();
	if (ImGui::Button("Hide (i)")) {
		_showGUI = false;
	}
	ImGui::SameLine();
	if(ImGui::Button("Display")){
		ImGui::OpenPopup("Display options");

	}
	SystemAction action = SystemAction::NONE;
	if(ImGui::BeginPopup("Display options")){
		if(ImGui::Checkbox("Fullscreen", &_fullscreen)){
			action = SystemAction::FULLSCREEN;
		}
		if(!_fullscreen){
			ImGui::PushItemWidth(100);
			ImGui::InputInt2("Window size", &_windowSize[0]);
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if(ImGui::Button("Resize")){
				action = SystemAction::RESIZE;
				action.data[0] = _windowSize[0];
				action.data[1] = _windowSize[1];
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine(340);
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		const std::string versionString = std::string("MIDIVisualizer v") + std::to_string(MIDIVIZ_VERSION_MAJOR) + "." + std::to_string(MIDIVIZ_VERSION_MINOR);
		ImGui::TextUnformatted(versionString.c_str());
		ImGui::TextUnformatted("Created by S. Rodriguez (kosua20)");
		ImGui::TextUnformatted("github.com/kosua20/MIDIVisualizer");
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
	return action;
}

void LiveRenderer::showParticleOptions(){
	ImGui::PushID("ParticlesSettings");

	if(channelColorEdit("Color##Particles", "Color", _state.particles.colors)){
		synchronizeColors(_state.particles.colors);
	}

	ImGui::SameLine(COLUMN_SIZE);

	ImGui::PushItemWidth(100);
	if (ImGui::InputFloat("Size", &_state.particles.scale, 1.0f, 10.0f)) {
		_state.particles.scale = std::max(1.0f, _state.particles.scale);
	}

	ImGui::PushItemWidth(COLUMN_SIZE-10);
	if (ImGui::SliderInt("Count", &_state.particles.count, 1, 512)) {
		_state.particles.count = std::min(std::max(_state.particles.count, 1), 512);
	}
	ImGui::PopItemWidth();

	const bool mp0 = ImGui::InputFloat("Speed", &_state.particles.speed, 0.001f, 1.0f);
	ImGui::SameLine(COLUMN_SIZE);
	const bool mp1 = ImGui::InputFloat(	"Expansion", &_state.particles.expansion, 0.1f, 5.0f);
	ImGui::PopItemWidth();

	if (mp1 || mp0) {
		_scene->setParticlesParameters(_state.particles.speed, _state.particles.expansion);
	}

	if (ImGui::Button("Load images...##Particles")) {
		// Read arguments.
		nfdpathset_t outPaths;
		nfdresult_t result = NFD_OpenDialogMultiple("png;jpg,jpeg;", NULL, &outPaths);

		if (result == NFD_OKAY) {
			std::vector<std::string> paths;
			for (size_t i = 0; i < NFD_PathSet_GetCount(&outPaths); ++i) {
				nfdchar_t *outPath = NFD_PathSet_GetPath(&outPaths, i);
				const std::string imageFilePath = std::string(outPath);
				paths.push_back(imageFilePath);
			}
			if (_state.particles.tex != ResourcesManager::getTextureFor("blankarray")) {
				glDeleteTextures(1, &_state.particles.tex);
			}
			_state.particles.tex = loadTextureArray(paths, false, _state.particles.texCount);
			NFD_PathSet_Free(&outPaths);
			if (_state.particles.scale <= 9.0f) {
				_state.particles.scale = 10.0f;
			}
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted("You can select multiple images (PNG or JPEG). They should be square and greyscale, where black is transparent, white opaque.");
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	ImGui::SameLine(COLUMN_SIZE);
	if (ImGui::Button("Clear images##TextureParticles")) {
		if (_state.particles.tex != ResourcesManager::getTextureFor("blankarray")) {
			glDeleteTextures(1, &_state.particles.tex);
		}
		// Use a white square particle appearance by default.
		_state.particles.tex =  ResourcesManager::getTextureFor("blankarray");
		_state.particles.texCount = 1;
		_state.particles.scale = 1.0f;
	}
	ImGui::PopID();
}

void LiveRenderer::showKeyboardOptions(){
	ImGui::PushItemWidth(25);
	if (ImGui::ColorEdit3("Color##Keys", &_state.background.keysColor[0], ImGuiColorEditFlags_NoInputs)) {
		_score->setColors(_state.background.linesColor, _state.background.textColor, _state.background.keysColor);
	}
	ImGui::PopItemWidth();
	ImGui::SameLine(COLUMN_SIZE);


	ImGui::PushItemWidth(100);
	if(ImGui::SliderFloat("Size##Keys", &_state.keyboard.size, 0.0f, 1.0f)){
		_state.keyboard.size = (std::min)((std::max)(_state.keyboard.size, 0.0f), 1.0f);
		_scene->setKeyboardSize(_state.keyboard.size);
		_score->setKeyboardSize(_state.keyboard.size);
	}
	ImGui::PopItemWidth();

	ImGui::Checkbox("Highlight keys", &_state.keyboard.highlightKeys);

	if (_state.keyboard.highlightKeys) {
		ImGui::Checkbox("Custom colors", &_state.keyboard.customKeyColors);
		if (_state.keyboard.customKeyColors) {

			ImGui::SameLine(COLUMN_SIZE);
			ImGui::PushItemWidth(25);
			if(ImGui::ColorEdit3("Major##KeysHighlight", &_state.keyboard.majorColor[0][0], ImGuiColorEditFlags_NoInputs)){
				// Ensure synchronization of the override array.
				for(size_t cid = 1; cid < _state.keyboard.majorColor.size(); ++cid){
					_state.keyboard.majorColor[cid] = _state.keyboard.majorColor[0];
				}
			}

			ImGui::SameLine(COLUMN_SIZE+80);
			if(ImGui::ColorEdit3("Minor##KeysHighlight", &_state.keyboard.minorColor[0][0], ImGuiColorEditFlags_NoInputs)){
				// Ensure synchronization of the override array.
				for(size_t cid = 1; cid < _state.keyboard.minorColor.size(); ++cid){
					_state.keyboard.minorColor[cid] = _state.keyboard.minorColor[0];
				}
			}
			ImGui::PopItemWidth();
		}
	}
}

void LiveRenderer::showPedalOptions(){
	ImGui::PushItemWidth(25);
	ImGui::ColorEdit3("Color##Pedals", &_state.pedals.color[0], ImGuiColorEditFlags_NoInputs);
	ImGui::PopItemWidth();
	ImGui::SameLine(COLUMN_SIZE);
	ImGui::Combo("Location", (int*)&_state.pedals.location, "Top left\0Bottom left\0Top right\0Bottom right\0");

	if(ImGui::SliderFloat("Opacity##Pedals", &_state.pedals.opacity, 0.0f, 1.0f)){
		_state.pedals.opacity = std::min(std::max(_state.pedals.opacity, 0.0f), 1.0f);
	}
	ImGui::SameLine(COLUMN_SIZE);
	if(ImGui::SliderFloat("Size##Pedals", &_state.pedals.size, 0.05f, 0.5f)){
		_state.pedals.size = std::min(std::max(_state.pedals.size, 0.05f), 0.5f);
	}
	ImGui::Checkbox("Merge pedals", &_state.pedals.merge);
}


void LiveRenderer::showWaveOptions(){
	ImGui::PushItemWidth(25);
	ImGui::ColorEdit3("Color##Waves", &_state.waves.color[0], ImGuiColorEditFlags_NoInputs);
	ImGui::PopItemWidth();
	ImGui::SameLine(COLUMN_SIZE);
	ImGui::SliderFloat("Amplitude##Waves", &_state.waves.amplitude, 0.0f, 5.0f);

	ImGui::SliderFloat("Spread##Waves", &_state.waves.spread, 0.0f, 5.0f);
	ImGui::SameLine(COLUMN_SIZE);
	ImGui::SliderFloat("Frequency##Waves", &_state.waves.frequency, 0.0f, 5.0f);

	if(ImGui::SliderFloat("Opacity##Waves", &_state.waves.opacity, 0.0f, 1.0f)){
		_state.waves.opacity = std::min(std::max(_state.waves.opacity, 0.0f), 1.0f);
	}

}

void LiveRenderer::showBlurOptions(){
	ImGui::Checkbox("Blur the notes", &_state.showBlurNotes);
	ImGui::SameLine(COLUMN_SIZE);
	ImGui::PushItemWidth(100);
	if (ImGui::SliderFloat("Fading", &_state.attenuation, 0.0f, 1.0f)) {
		_state.attenuation = std::min(1.0f, std::max(0.0f, _state.attenuation));
		glUseProgram(_blurringScreen.programId());
		const GLuint id1 = glGetUniformLocation(_blurringScreen.programId(), "attenuationFactor");
		glUniform1f(id1, _state.attenuation);
		glUseProgram(0);
	}
	ImGui::PopItemWidth();
}

void LiveRenderer::showScoreOptions(){
	ImGui::PushItemWidth(25);
	const bool cbg0 = ImGui::ColorEdit3("Lines##Background", &_state.background.linesColor[0], ImGuiColorEditFlags_NoInputs);
	ImGui::SameLine();
	const bool cbg1 = ImGui::ColorEdit3("Text##Background", &_state.background.textColor[0], ImGuiColorEditFlags_NoInputs);
	ImGui::PopItemWidth();
	ImGui::SameLine(COLUMN_SIZE);
	const bool m1 = ImGui::Checkbox("Digits", &_state.background.digits);
	const bool m2 = ImGui::Checkbox("Horizontal lines", &_state.background.hLines);
	ImGui::SameLine(COLUMN_SIZE);
	const bool m3 = ImGui::Checkbox("Vertical lines", &_state.background.vLines);

	if (m1 || m2 || m3) {
		_score->setDisplay(_state.background.digits, _state.background.hLines, _state.background.vLines);
	}

	if (cbg0 || cbg1) {
		_score->setColors(_state.background.linesColor, _state.background.textColor, _state.background.keysColor);
	}
}

void LiveRenderer::showBackgroundOptions(){
	ImGui::PushItemWidth(25);
	ImGui::ColorEdit3("Color##Background", &_state.background.color[0],
		ImGuiColorEditFlags_NoInputs);
	ImGui::PopItemWidth();
	ImGui::SameLine(COLUMN_SIZE);
	ImGui::PushItemWidth(100);
	if (ImGui::SliderFloat("Opacity##Background", &_state.background.imageAlpha, 0.0f, 1.0f)) {
		_state.background.imageAlpha = std::min(std::max(_state.background.imageAlpha, 0.0f), 1.0f);
	}
	ImGui::PopItemWidth();
	if (ImGui::Button("Load image...##Background")){
		// Read arguments.
		nfdchar_t *outPath = NULL;
		nfdresult_t result = NFD_OpenDialog("jpg,jpeg;png", NULL, &outPath);
		if (result == NFD_OKAY) {
			glDeleteTextures(1, &_state.background.tex);
			_state.background.tex = loadTexture(std::string(outPath), 4, false);
			_state.background.image = true;
			// Ensure minimal visibility.
			if (_state.background.imageAlpha < 0.1f) {
				_state.background.imageAlpha = 0.1f;
			}
		}
	}
	ImGui::SameLine(COLUMN_SIZE);
	if (ImGui::Button("Clear image##Background")) {
		_state.background.image = false;
		glDeleteTextures(1, &_state.background.tex);
		_state.background.tex = 0;
	}
	ImGui::Checkbox("Image extends under keyboard", &_state.background.imageBehindKeyboard);

}

void LiveRenderer::showBottomButtons(){
	if(ImGui::Button("Export...")){
		ImGui::OpenPopup("Export");
	}
	ImGui::SameLine();

	if (ImGui::Button("Save config...")) {
		// Read arguments.
		nfdchar_t *savePath = NULL;
		nfdresult_t result = NFD_SaveDialog("ini", NULL, &savePath);
		if (result == NFD_OKAY) {
			_state.save(std::string(savePath));
		}
	}
	ImGui::SameLine();

	if (ImGui::Button("Load config...")) {
		// Read arguments.
		nfdchar_t *outPath = NULL;
		nfdresult_t result = NFD_OpenDialog("ini", NULL, &outPath);
		if (result == NFD_OKAY) {
			_state.load(std::string(outPath));
			setState(_state);
		}
	}
	ImGui::SameLine();

	if (ImGui::Button("Reset##config")) {
		_state.reset();
		setState(_state);
	}
}

void LiveRenderer::showLayers() {
	const ImVec2 & screenSize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(screenSize.x * 0.5f, screenSize.y * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
	
	if (ImGui::Begin("Layers", &_showLayers)) {
		ImGui::TextDisabled("You can drag and drop layers to reorder them.");
		for (int i = int(_state.layersMap.size()) - 1; i >= 0; --i) {
			const int layerId = _state.layersMap[i];
			if (layerId >= _layers.size()) {
				continue;
			}
			auto & layer = _layers[layerId];
			if (layer.type == Layer::BGCOLOR) {
				continue;
			}
			ImGui::Separator();
			ImGui::PushID(layerId);

			ImGui::Checkbox("##LayerCheckbox", layer.toggle);
			ImGui::SameLine();
			ImGui::Selectable(layer.name.c_str());

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				ImGui::Text("%s", layer.name.c_str());
				ImGui::SetDragDropPayload("REORDER_LAYER", &i, sizeof(int));
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REORDER_LAYER"))
				{
					int iPayload = *(const int*)payload->Data;
					int newId = _state.layersMap[iPayload];
					// Instead of just swapping, we shift all intermediate indices.
					const int ddlt = (iPayload <= i ? 1 : -1);
					for (int lid = iPayload; lid != i; lid += ddlt) {
						_state.layersMap[lid] = _state.layersMap[lid + ddlt];
					}
					_state.layersMap[i] = newId;
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopID();
		}
		ImGui::Separator();
	}
	ImGui::End();
}

void LiveRenderer::showSets(){
	if(ImGui::BeginPopup("Note sets options")){
		ImGui::Text("Decide how notes should be grouped in multiple sets");
		ImGui::Text("(to which you can assign different key/effects colors).");
		ImGui::Text("This can be based on the MIDI channel, the track or by");
		ImGui::Text("separating notes that are lower or higher than a given key.");

		bool shouldUpdate = false;
		shouldUpdate = ImGui::RadioButton("Channel", (int*)(&_state.setOptions.mode), int(SetMode::CHANNEL)) || shouldUpdate;
		ImGui::SameLine(120);
		shouldUpdate = ImGui::RadioButton("Track", (int*)(&_state.setOptions.mode), int(SetMode::TRACK)) || shouldUpdate;
		ImGui::SameLine(2*120);
		shouldUpdate = ImGui::RadioButton("Key", (int*)(&_state.setOptions.mode), int(SetMode::KEY)) || shouldUpdate;
		ImGui::SameLine();

		ImGui::PushItemWidth(100);
		shouldUpdate = ImGui::Combo("##key", &_state.setOptions.key, midiKeysString) || shouldUpdate;
		ImGui::PopItemWidth();

		if(shouldUpdate){
			_scene->updateSets(_state.setOptions);
		}
		ImGui::EndPopup();
	}
}

void LiveRenderer::applyAllSettings() {
	// Apply all modifications.

	// One-shot parameters.
	_scene->setScaleAndMinorWidth(_state.scale, _state.background.minorsWidth);
	_score->setScaleAndMinorWidth(_state.scale, _state.background.minorsWidth);
	_scene->setParticlesParameters(_state.particles.speed, _state.particles.expansion);
	_score->setDisplay(_state.background.digits, _state.background.hLines, _state.background.vLines);
	_score->setColors(_state.background.linesColor, _state.background.textColor, _state.background.keysColor);
	_scene->setKeyboardSize(_state.keyboard.size);
	_score->setKeyboardSize(_state.keyboard.size);

	updateMinMaxKeys();

	// Reset buffers.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	_particlesFramebuffer->bind();
	glClear(GL_COLOR_BUFFER_BIT);
	_particlesFramebuffer->unbind();
	_blurFramebuffer->bind();
	glClear(GL_COLOR_BUFFER_BIT);
	_blurFramebuffer->unbind();
	glUseProgram(_blurringScreen.programId());
	GLuint id2 = glGetUniformLocation(_blurringScreen.programId(), "attenuationFactor");
	glUniform1f(id2, _state.attenuation);
	glUseProgram(0);

	// Resize the framebuffers.
	updateSizes();

	// Finally, restore the track at the beginning.
	reset();
	// All other parameters are directly used at render.
}

void LiveRenderer::clean() {

	// Clean objects.
	_scene->clean();
	_score->clean();
	_blurringScreen.clean();
	_passthrough.clean();
	_backgroundTexture.clean();
	_fxaa.clean();
	_particlesFramebuffer->clean();
	_blurFramebuffer->clean();
	_finalFramebuffer->clean();
	_renderFramebuffer->clean();
}

void LiveRenderer::rescale(float scale){
	resizeAndRescale(_camera.screenSize()[0], _camera.screenSize()[1], scale);
}

void LiveRenderer::resize(int width, int height) {
	resizeAndRescale(width, height, _camera.scale());
}

void LiveRenderer::resizeAndRescale(int width, int height, float scale) {
	// Update the projection matrix.
	_camera.screen(width, height, scale);
	updateSizes();
}

void LiveRenderer::updateSizes(){
	// Resize the framebuffers.
	const auto &currentQuality = Quality::availables.at(_state.quality);
	const glm::vec2 baseRes(_camera.renderSize());
	_particlesFramebuffer->resize(currentQuality.particlesResolution * baseRes);
	_blurFramebuffer->resize(currentQuality.blurResolution * baseRes);
	_renderFramebuffer->resize(currentQuality.finalResolution * baseRes);
	_finalFramebuffer->resize(currentQuality.finalResolution * baseRes);
	//_recorder.setSize(glm::ivec2(_finalFramebuffer->_width, _finalFramebuffer->_height));
}

void LiveRenderer::keyPressed(int key, int action) {

	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_P) {
			_shouldPlay = !_shouldPlay;
			_timerStart = DEBUG_SPEED * float(glfwGetTime()) - _timer;
		}
		else if (key == GLFW_KEY_R) {
			reset();
		}
		else if (key == GLFW_KEY_I) {
			_showGUI = !_showGUI;
		}
		else if (key == GLFW_KEY_D) {
			_showDebug = !_showDebug;
		}
	}
}

void LiveRenderer::reset() {
	_timer = -_state.prerollTime;
	_timerStart = DEBUG_SPEED * float(glfwGetTime()) + (_shouldPlay ? _state.prerollTime : 0.0f);
	_scene->resetParticles();
}

void LiveRenderer::setState(const State & state){
	_state = state;

	// Update toggles.
	_layers[Layer::BGTEXTURE].toggle = &_state.background.image;
	_layers[Layer::BLUR].toggle = &_state.showBlur;
	_layers[Layer::ANNOTATIONS].toggle = &_state.showScore;
	_layers[Layer::KEYBOARD].toggle = &_state.showKeyboard;
	_layers[Layer::PARTICLES].toggle = &_state.showParticles;
	_layers[Layer::NOTES].toggle = &_state.showNotes;
	_layers[Layer::FLASHES].toggle = &_state.showFlashes;
	_layers[Layer::PEDAL].toggle = &_state.showPedal;
	_layers[Layer::WAVE].toggle = &_state.showWave;

	// Update split notes.
	if(_scene){
		_scene->updateSets(_state.setOptions);
	}
	applyAllSettings();
}

void LiveRenderer::startDirectRecording(const std::string & path, Recorder::Format format, int framerate, int bitrate, bool skipBackground, const glm::vec2 & size){
	
}

void LiveRenderer::startRecording(){
	
}

bool LiveRenderer::channelColorEdit(const char * name, const char * displayName, ColorArray & colors){
	if(!_state.perChannelColors){
		// If locked, display color sink.
		ImGui::PushItemWidth(25);
		const bool inter = ImGui::ColorEdit3(name, &colors[0][0], ImGuiColorEditFlags_NoInputs);
		ImGui::PopItemWidth();
		if(inter){
			// Ensure synchronization.
			for(size_t cid = 1; cid < colors.size(); ++cid){
				colors[cid] = colors[0];
			}
		}
		return inter;
	}

	// Else, we display a drop down and a popup.
	if(ImGui::ArrowButton(name, ImGuiDir_Down)){
		ImGui::OpenPopup(name);
	}
	ImGui::SameLine(); ImGui::Text("%s", displayName);

	if(ImGui::BeginPopup(name)){
		// Do 2x4 color sinks.
		bool edit = false;
		ImGui::PushItemWidth(25);
		for(size_t cid = 0; cid < colors.size(); ++cid){
			const std::string nameC = "Set " + std::to_string(cid);
			edit = ImGui::ColorEdit3(nameC.c_str(), &colors[cid][0], ImGuiColorEditFlags_NoInputs) || edit;
			if(cid % 2 == 0 && cid != colors.size()-1){
				ImGui::SameLine();
			}
		}
		ImGui::PopItemWidth();
		ImGui::EndPopup();
		return edit;
	}
	return false;
}


void LiveRenderer::updateMinMaxKeys(){
	// Make sure keys are properly ordered.
	if(_state.minKey > _state.maxKey){
		std::swap(_state.minKey, _state.maxKey);
	}
	// Convert to "major" only indices.
	const int minKeyMaj = (_state.minKey/12) * 7 + noteShift[_state.minKey % 12];
	const int maxKeyMaj = (_state.maxKey/12) * 7 + noteShift[_state.maxKey % 12];
	const int noteCount = (maxKeyMaj - minKeyMaj + 1);

	_scene->setMinMaxKeys(_state.minKey, minKeyMaj, noteCount);
	_score->setMinMaxKeys(_state.minKey, minKeyMaj, noteCount);
}
