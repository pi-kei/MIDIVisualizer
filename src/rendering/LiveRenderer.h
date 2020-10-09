#ifndef LiveRenderer_h
#define LiveRenderer_h
#include <GLFW/glfw3.h>
#include <gl3w/gl3w.h>
#include <glm/glm.hpp>
#include <memory>
#include <array>

#include "Framebuffer.h"
#include "camera/Camera.h"
#include "LiveMIDIScene.h"
#include "ScreenQuad.h"
#include "Score.h"

#include "../helpers/Recorder.h"

#include "State.h"

#define DEBUG_SPEED (1.0f)

struct SystemAction {
	enum Type {
		NONE, FIX_SIZE, FREE_SIZE, FULLSCREEN, QUIT, RESIZE
	};

	Type type;
	glm::ivec4 data;

	SystemAction(Type action);
};


class LiveRenderer {

public:

	LiveRenderer(int winW, int winH, bool fullscreen);

	~LiveRenderer();
	
	bool loadFile(const std::string & midiFilePath);

	void setState(const State & state);
	
	/// Draw function
	SystemAction draw(const float currentTime);
	
	/// Clean function
	void clean();

	/// Handle screen resizing
	void resize(int width, int height);

	/// Handle window content density.
	void rescale(float scale);

	void resizeAndRescale(int width, int height, float scale);

	/// Handle keyboard inputs
	void keyPressed(int key, int action);

	/// Diretly start recording.
	void startDirectRecording(const std::string & path, Recorder::Format format, int framerate, int bitrate, bool skipBackground, const glm::vec2 & size);
	
private:
	

	struct Layer {
		
		enum Type : unsigned int {
			BGCOLOR = 0, BGTEXTURE, BLUR, ANNOTATIONS, KEYBOARD, PARTICLES, NOTES, FLASHES, PEDAL, WAVE, COUNT
		};

		Type type = BGCOLOR;
		std::string name = "None";
		void (LiveRenderer::*draw)(const glm::vec2 &) = nullptr;
		bool * toggle = nullptr;

	};

	void blurPrepass();

	void drawBackgroundImage(const glm::vec2 & invSize);

	void drawBlur(const glm::vec2 & invSize);

	void drawParticles(const glm::vec2 & invSize);

	void drawScore(const glm::vec2 & invSize);

	void drawKeyboard(const glm::vec2 & invSize);

	void drawNotes(const glm::vec2 & invSize);

	void drawFlashes(const glm::vec2 & invSize);

	void drawPedals(const glm::vec2 & invSize);
	
	void drawWaves(const glm::vec2 & invSize);

	SystemAction drawGUI(const float currentTime);

	void drawScene(bool transparentBG);

	SystemAction showTopButtons(double currentTime);

	void showParticleOptions();

	void showKeyboardOptions();

	void showPedalOptions();
	
	void showWaveOptions();

	void showBlurOptions();

	void showScoreOptions();

	void showBackgroundOptions();

	void showBottomButtons();

	void showLayers();

	void showSets();

	void applyAllSettings();
	
	void reset();

	void startRecording();

	void updateSizes();

	bool channelColorEdit(const char * name, const char * displayName, ColorArray & colors);
	
	void updateMinMaxKeys();

	void synchronizeColors(const ColorArray & colors);

	State _state;
	std::array<Layer, Layer::COUNT> _layers;

	float _timer;
	float _timerStart;
	bool _shouldPlay;
	bool _showGUI;
	bool _showDebug;

	//Recorder _recorder;
	
	Camera _camera;
	
	std::shared_ptr<Framebuffer> _particlesFramebuffer;
	std::shared_ptr<Framebuffer> _blurFramebuffer;
	std::shared_ptr<Framebuffer> _renderFramebuffer;
	std::shared_ptr<Framebuffer> _finalFramebuffer;

	std::shared_ptr<LiveMIDIScene> _scene;
	ScreenQuad _blurringScreen;
	ScreenQuad _passthrough;
	ScreenQuad _backgroundTexture;
	ScreenQuad _fxaa;
	std::shared_ptr<Score> _score;

	glm::ivec2 _windowSize;
	bool _showLayers = false;
	bool _exitAfterRecording = false;
	bool _fullscreen = false;
};

#endif
