#ifndef LiveMIDIScene_h
#define LiveMIDIScene_h
#include <mutex>
#include <queue>
#include <gl3w/gl3w.h>
#include <glm/glm.hpp>
#include <rtmidi17/rtmidi17.hpp>
#include "../midi/MIDIFile.h"
#include "State.h"

class LiveMIDIScene {

public:

	LiveMIDIScene();

	LiveMIDIScene(const std::string & midiFilePath, const SetOptions & options);

	void updateSets(const SetOptions & options);
	
	~LiveMIDIScene();
	
	void updatesActiveNotes(double time);
	
	/// Draw function
	void drawNotes(float time, const glm::vec2 & invScreenSize, const ColorArray & majorColors, const ColorArray & minorColors, bool prepass);
	
	void drawFlashes(float time, const glm::vec2 & invScreenSize, const ColorArray & baseColors, float userScale);
	
	void drawParticles(float time, const glm::vec2 & invScreenSize, const State::ParticlesState & state, bool prepass);
	
	void drawKeyboard(float time, const glm::vec2 & invScreenSize, const glm::vec3 & keyColor, const ColorArray & majorColors, const ColorArray & minorColors, bool highlightKeys);

	void drawPedals(float time, const glm::vec2 & invScreenSize, const State::PedalsState & state, float keyboardHeight);

	void drawWaves(float time, const glm::vec2 & invScreenSize, const State::WaveState & state, float keyboardHeight);

	/// Clean function
	void clean();

	//const MIDIFile& midiFile() { return _midiFile; }
	
	void setScaleAndMinorWidth(const float scale, const float minorWidth);

	void setParticlesParameters(const float speed, const float expansion);

	void setKeyboardSize(float keyboardHeight);

	void setMinMaxKeys(int minKey, int minKeyMajor, int notesCount);

	const double & duration() const { return _duration/*_midiFile.duration()*/; };
	
	void resetParticles();

	const double & secondsPerMeasure() const { return _secondsPerMeasure; }

	const int & notesCount() const { return _count; }

	void updateNotesBuffer(double time);

private:

	void renderSetup();

	void upload(const std::vector<float> & data);

	GLuint _programId;
	GLuint _programFlashesId;
	GLuint _programParticulesId;
	GLuint _programKeysId;
	GLuint _programPedalsId;
	GLuint _programWaveId;
	
	GLuint _vao;
	GLuint _ebo;
	GLuint _dataBuffer;
	
	GLuint _flagsBufferId;
	GLuint _vaoFlashes;
	GLuint _texFlash;
	
	GLuint _vaoParticles;
	GLuint _texParticles;

	GLuint _vaoKeyboard;
	GLuint _uboKeyboard;

	GLuint _vaoPedals;
	size_t _countPedals;

	GLuint _vaoWave;
	size_t _countWave;
	
	size_t _primitiveCount;
	
	std::array<int, 128> _actives;

	std::vector<float> _data;

	struct Particles {
		int note = -1;
		int set = -1;
		float duration = 0.0f;
		float start = 1000000.0f;
		float elapsed = 0.0f;
	};
	std::vector<Particles> _particles;
	double _previousTime;

	//MIDIFile _midiFile;
	double _secondsPerMeasure = 1.0;
	int _count = 0;
	double _duration = 20.0;

	rtmidi::midi_in _midiin;
	std::vector<MIDINote> _notes;
	double _lastMidiMessageTime = 0.0;

	std::mutex _queueMutex;
	std::queue<rtmidi::message> _midiMessagesQueue;
	
};

#endif
