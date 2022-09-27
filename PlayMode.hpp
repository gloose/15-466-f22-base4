#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include "../nest-libs/windows/glm/include/glm/glm.hpp"

#include <vector>
#include <deque>
#include <array>

#include "../nest-libs/windows/harfbuzz/include/hb.h"
#include "../nest-libs/windows/harfbuzz/include/hb-ft.h"
#include "../nest-libs/windows/freetype/include/freetype/freetype.h"
#include "../nest-libs/windows/freetype/include/freetype/fttypes.h"

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	// Adapted from PPU466
	glm::u8vec3 background_color = glm::u8vec3(0x00, 0x00, 0x00);
	enum : uint32_t {
		ScreenWidth = 1280,
		ScreenHeight = 720
	};

	//In order to implement the PPU466 on modern graphics hardware, a fancy, special purpose tile-drawing shader is used:
	struct PPUTileProgram {
		PPUTileProgram();
		~PPUTileProgram();

		GLuint program = 0;

		//Attribute (per-vertex variable) locations:
		GLuint Position_vec2 = -1U;
		GLuint TileCoord_ivec2 = -1U;

		//Uniform (per-invocation variable) locations:
		GLuint OBJECT_TO_CLIP_mat4 = -1U;

		//Textures bindings:
		//TEXTURE0 - the tile table (as a 128x128 R8UI texture)
	};

	//PPU data is streamed to the GPU (read: uploaded 'just in time') using a few buffers:
	struct PPUDataStream {
		PPUDataStream();
		~PPUDataStream();

		//vertex format for convenience:
		struct Vertex {
			Vertex(glm::ivec2 const& Position_, glm::ivec2 const& TileCoord_)
				: Position(Position_), TileCoord(TileCoord_) { }
			//I generally make class members lowercase, but I make an exception here because
			// I use uppercase for vertex attributes in shader programs and want to match.
			glm::ivec2 Position;
			glm::ivec2 TileCoord;
		};

		//vertex buffer that will store data stream:
		GLuint vertex_buffer = 0;

		//vertex array object that maps tile program attributes to vertex storage:
		GLuint vertex_buffer_for_tile_program = 0;

		//texture object that will store tile table:
		GLuint tile_tex = 0;
	};

	// Struct representing a line of text
	struct Line {
		size_t text_start = 0;
		size_t text_end = 0;
		bool spoken = false;
		size_t speaker_start = 0;
		size_t speaker_end = 0;
	};

	struct Condition {
		size_t name_start = 0;
		size_t name_end = 0;
		bool negated = false;
	};

	struct Transition {
		size_t trigger_start = 0;
		size_t trigger_end = 0;
		size_t preconditions_start = 0;
		size_t preconditions_end = 0;
		size_t postconditions_start = 0;
		size_t postconditions_end = 0;
	};

	// Struct representing a game state
	struct State {
		std::string name;
		std::vector<Line> lines;
		std::vector<Transition> transitions;
		std::vector<Condition> conditions;
		std::vector<char> string_data;
	};

	std::unordered_map<std::string, State> states;
	std::string current_state;

	// Struct representing a clickable trigger phrase
	struct Trigger {
		std::string name;
		glm::vec2 position;
		glm::vec2 size;
	};

	std::vector<Trigger> triggers;

	struct Timeline {
		int index = 0;
		int date = 0;
		std::vector<State> states;
	};

	std::vector<Timeline> timelines;
	size_t current_timeline;
	int timeline_width = 600;
	int state_width = (int)(timeline_width * 0.9f);

	int scroll_y = 0;
	int scroll_speed = 32;
	bool scroll_to_new_state = false;

	// Properties of the font used in the game
	FT_Library ft_library;
	FT_Face ft_face;
	hb_font_t* hb_font;
	uint32_t char_top = 1;
	uint32_t char_bottom = 1;
	uint32_t char_width = 1;
	uint32_t char_height = 1;
	uint32_t min_char = 32;
	uint32_t max_char = 126;
	uint32_t num_chars = max_char - min_char + 1;
	int font_size = 24;

	// Helper functions
	int drawText(std::string text, glm::vec2 position, size_t width, std::vector<PPUDataStream::Vertex>* triangle_strip);
	void drawTriangleStrip(const std::vector<PPUDataStream::Vertex>& triangle_strip);
	int drawState(const State& state, glm::ivec2 position, std::vector<PPUDataStream::Vertex>* triangle_strip);
	void drawTimeline(const Timeline& timeline, std::vector<PPUDataStream::Vertex>* triangle_strip);
	std::string stateText(const State& state, size_t start, size_t end);
	std::string lineText(const State& state, size_t line_num);
	void useTrigger(std::string name);
};
