#include "PlayMode.hpp"
#include "LitColorTextureProgram.hpp"
#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "GL.hpp"
#include "gl_compile_program.hpp"
#include "read_write_chunk.hpp"

//#include "../nest-libs/windows/glm/include/glm/gtc/type_ptr.hpp"
//#include "../nest-libs/windows/harfbuzz/include/hb.h"
//#include "../nest-libs/windows/harfbuzz/include/hb-ft.h"
//#include "../nest-libs/windows/freetype/include/freetype/freetype.h"
//#include "../nest-libs/windows/freetype/include/freetype/fttypes.h"
#include <glm/gtc/type_ptr.hpp>
#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>
#include <freetype/fttypes.h>

#include <random>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>

Load< PlayMode::PPUTileProgram > tile_program(LoadTagEarly); //will 'new PPUTileProgram()' by default
Load< PlayMode::PPUDataStream > data_stream(LoadTagDefault);

PlayMode::PlayMode() {
	// Adapted from Harfbuzz example linked on assignment page
	// This font was obtained from https://fonts.google.com/specimen/Roboto
	// See the license in dist/Robot/LICENSE.txt
	std::string fontfilestring = data_path("Roboto/Roboto-Black.ttf");
	const char* fontfile = fontfilestring.c_str();

	// Initialize FreeType and create FreeType font face.
	if (FT_Init_FreeType(&ft_library))
		abort();
	if (FT_New_Face(ft_library, fontfile, 0, &ft_face))
		abort();
	if (FT_Set_Char_Size(ft_face, font_size * 64, font_size * 64, 0, 0))
		abort();

	// Create hb-ft font.
	hb_font = hb_ft_font_create(ft_face, NULL);

	// Determine a fixed size and baseline for all character tiles
	for (size_t i = min_char; i <= max_char; i++) {
		FT_UInt glyph_index = FT_Get_Char_Index(ft_face, (char)i);
		FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
		FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);

		uint32_t w = ft_face->glyph->bitmap.width + ft_face->glyph->bitmap_left;

		int top = ft_face->glyph->bitmap_top;
		int bottom = ft_face->glyph->bitmap.rows - top;

		if (w > char_width) {
			char_width = w;
		}
		if (top > (int)char_top) {
			char_top = top;
		}
		if (bottom > (int)char_bottom) {
			char_bottom = bottom;
		}
	}
	char_height = char_top + char_bottom;

	//interpret tiles and build a 1 x num_chars color texture (adapated from PPU466)
	std::vector<glm::u8vec4> data;
	data.resize(num_chars * char_width * char_height);
	for (uint32_t i = 0; i < num_chars; i++) {
		FT_UInt glyph_index = FT_Get_Char_Index(ft_face, (char)(min_char + i));
		FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
		FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);

		//location of tile in the texture:
		uint32_t ox = i * char_width;

		//copy tile indices into texture:
		for (int y = 0; y < (int)char_height; y++) {
			for (int x = 0; x < (int)char_width; x++) {
				int bitmap_x = x - ft_face->glyph->bitmap_left;
				int bitmap_baseline = ft_face->glyph->bitmap_top;
				int from_baseline = y - char_bottom;
				int bitmap_y = bitmap_baseline - from_baseline;

				if (bitmap_x >= 0 && bitmap_x < (int)ft_face->glyph->bitmap.width && bitmap_y >= 0 && bitmap_y < (int)ft_face->glyph->bitmap.rows) {
					data[ox + x + (char_width * num_chars) * y] = glm::u8vec4(0xff, 0xff, 0xff, ft_face->glyph->bitmap.buffer[bitmap_x + ft_face->glyph->bitmap.width * bitmap_y]);
				}
				else {
					data[ox + x + (char_width * num_chars) * y] = glm::u8vec4(0, 0, 0, 0);
				}
			}
		}
	}
	glBindTexture(GL_TEXTURE_2D, data_stream->tile_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, char_width * num_chars, char_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	for (const auto& file : std::filesystem::directory_iterator(data_path("assets/states"))) {
		std::string name = file.path().filename().string();
		State state;
		std::ifstream ifile(data_path("assets/states/" + name), std::ios::binary);
		read_chunk(ifile, "line", &state.lines);
		read_chunk(ifile, "tran", &state.transitions);
		read_chunk(ifile, "cond", &state.conditions);
		read_chunk(ifile, "strn", &state.string_data);
		state.name = name;
		ifile.close();
		states[name] = state;
	}
	current_state = "start";

	timelines.emplace_back();
	timelines.back().index = 0;
	timelines.back().states.push_back(states[current_state]);
	timelines.back().date = 2094;
	current_timeline = 0;
}

PlayMode::~PlayMode() {
}

void PlayMode::useTrigger(std::string name) {
	for (size_t i = 0; i < states[current_state].transitions.size(); i++) {
		Transition& transition = states[current_state].transitions[i];
		if (stateText(states[current_state], transition.trigger_start, transition.trigger_end) == name) {
			// Found a transition for this trigger
			
			std::string new_state = stateText(states[current_state], states[current_state].conditions[transition.postconditions_start].name_start, states[current_state].conditions[transition.postconditions_start].name_end);
			if (states.find(new_state) != states.end()) {
				// Found the new state

				int target_date = timelines[current_timeline].date;
				bool new_timeline = false;
				if (name == "Go to 2034") {
					target_date = 2034;
					new_timeline = true;
				} else if (name == "15 YEARS AGO") {
					target_date = 2019;
					new_timeline = true;
				}

				if (new_timeline) {
					timelines.emplace_back();
					timelines.back().date = target_date;
					timelines.back().index = (int)timelines.size() - 1;
					current_timeline = timelines.back().index;
					observing_timeline = (int)current_timeline;
				}
				
				timelines[current_timeline].states.push_back(states[new_state]);
				scroll_to_timeline_end = true;
				current_state = new_state;
				observing_timeline = (int)current_timeline;
			}
		}
	}
}

bool PlayMode::handle_event(SDL_Event const& evt, glm::uvec2 const& window_size) {
	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			observing_timeline = std::max(observing_timeline - 1, 0);
			scroll_to_timeline_end = true;
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			observing_timeline = std::min(observing_timeline + 1, (int)timelines.size() - 1);
			scroll_to_timeline_end = true;
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		int x, y;
		SDL_GetMouseState(&x, &y);
		y = ScreenHeight - y;
		y += scroll_y;
		x += scroll_x;
		for (size_t i = 0; i < triggers.size(); i++) {
			Trigger& trigger = triggers[i];
			if (x > trigger.position.x && x < trigger.position.x + trigger.size.x && y > trigger.position.y && y < trigger.position.y + trigger.size.y) {
				useTrigger(trigger.name);
			}
		}
		return true;
	} else if (evt.type == SDL_MOUSEWHEEL) {
		scroll_y += evt.wheel.y * scroll_speed;
	}

	return false;
}

void PlayMode::update(float elapsed) {
	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

int PlayMode::drawText(std::string text, glm::vec2 position, size_t width, std::vector<PPUDataStream::Vertex>* triangle_strip, glm::u8vec4 color) {
	//helper to put a single tile somewhere on the screen:
	auto draw_tile = [&](glm::ivec2 const& lower_left, uint8_t tile_index, glm::u8vec4 tile_color) {
		float font_multiplier = (float)font_size / char_height;

		//convert tile index to lower-left pixel coordinate in tile image:
		glm::ivec2 tile_coord = glm::ivec2(tile_index * char_width, 0);

		//build a quad as a (very short) triangle strip that starts and ends with degenerate triangles:
		triangle_strip->emplace_back(glm::ivec2(lower_left.x + 0, lower_left.y - (int)(char_bottom * font_multiplier)), glm::ivec2(tile_coord.x + 0, tile_coord.y + 0), tile_color);
		triangle_strip->emplace_back(triangle_strip->back());
		triangle_strip->emplace_back(glm::ivec2(lower_left.x + 0, lower_left.y + (int)(char_top * font_multiplier)), glm::ivec2(tile_coord.x + 0, tile_coord.y + char_height), tile_color);
		triangle_strip->emplace_back(glm::ivec2(lower_left.x + (int)(char_width * font_multiplier), lower_left.y - (int)(char_bottom * font_multiplier)), glm::ivec2(tile_coord.x + char_width, tile_coord.y + 0), tile_color);
		triangle_strip->emplace_back(glm::ivec2(lower_left.x + (int)(char_width * font_multiplier), lower_left.y + (int)(char_top * font_multiplier)), glm::ivec2(tile_coord.x + char_width, tile_coord.y + char_height), tile_color);
		triangle_strip->emplace_back(triangle_strip->back());
	};

	const char* text_c_str = text.c_str();
	size_t start_line = 0;
	size_t line_num = 0;

	while (start_line < text.size()) {
		line_num++;

		// Create hb-buffer and populate.
		hb_buffer_t* hb_buffer;
		hb_buffer = hb_buffer_create();
		hb_buffer_add_utf8(hb_buffer, text_c_str + start_line, -1, 0, -1);
		hb_buffer_guess_segment_properties(hb_buffer);

		// Shape it!
		hb_feature_t feature;
		hb_feature_from_string("-liga", -1, &feature);
		hb_shape(hb_font, hb_buffer, &feature, 1);

		// Get glyph information and positions out of the buffer.
		unsigned int len = hb_buffer_get_length(hb_buffer);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer, NULL);

		// Draw text
		double current_x = position.x;
		double current_y = position.y - line_num * font_size;
		bool in_trigger = false;
		Trigger trigger;
		for (size_t i = 0; i < len; i++)
		{
			// Populate trigger struct for bracketed text
			if (text[start_line + i] == '[') {
				in_trigger = true;
				trigger.name = "";
				trigger.position = glm::vec2(current_x, current_y);
			}
			if (in_trigger && text[start_line + i] != '[' && text[start_line + i] != ']') {
				trigger.name = trigger.name + text[start_line + i];
			}
			if (in_trigger && text[start_line + i] == ']') {
				in_trigger = false;
				trigger.size = glm::vec2(current_x - trigger.position.x, font_size);
				triggers.push_back(trigger);
			}

			// Line break if next word would overflow
			if (!in_trigger && text[start_line + i] == ' ') {
				double cx = current_x + pos[i].x_advance / 64.;
				bool line_break = false;
				bool trigger_word = false;
				for (size_t j = i + 1; j < len; j++) {
					if (text[start_line + j] == '[') {
						trigger_word = true;
					}
					if (text[start_line + j] == ']') {
						trigger_word = false;
					}
					if (text[start_line + j] == ' ' && !trigger_word) {
						break;
					}
					cx += pos[j].x_advance / 64.;
					if (cx + char_width > position.x + width) {
						line_break = true;
						break;
					}
				}
				if (line_break) {
					start_line = start_line + i + 1;
					break;
				}
			}

			// Draw character
			glm::u8vec4 tile_color = color;
			if (in_trigger || text[start_line + i] == ']') {
				tile_color = trigger_color;
			}
			draw_tile(glm::ivec2((int)(current_x + pos[i].x_offset / 64.), (int)(current_y + pos[i].y_offset / 64.)), (uint8_t)text[start_line + i] - (uint8_t)min_char, tile_color);
			
			// Advance position
			current_x += pos[i].x_advance / 64.;
			current_y += pos[i].y_advance / 64.;
			
			// Line break on overflow (may be necessary if there are no spaces)
			if (current_x + char_width > position.x + width || i == len - 1) {
				start_line = start_line + i + 1;
				break;
			}
			
		}
	}
	
	return (int)(line_num * font_size);

	// TODO: start doing size estimation again?
	//assert(triangle_strip.size() == TristripSize && "Triangle strip size was estimated exactly.");
}

std::string PlayMode::stateText(const State& state, size_t start, size_t end) {
	return std::string(state.string_data.begin() + start, state.string_data.begin() + end);
}

std::string PlayMode::lineText(const State& state, size_t line_num) {
	Line line = state.lines[line_num];
	std::string ret = "";
	if (line.spoken) {
		ret = stateText(state, line.speaker_start, line.speaker_end) + ": ";
	}
	return ret + stateText(state, line.text_start, line.text_end);
}

int PlayMode::drawState(const State& state, glm::ivec2 position, std::vector<PPUDataStream::Vertex>* triangle_strip) {
	int y = position.y;
	for (size_t i = 0; i < state.lines.size(); i ++) {
		// Set character-specific colors
		glm::u8vec4 color = default_color;
		std::string speaker = stateText(state, state.lines[i].speaker_start, state.lines[i].speaker_end);
		if (speaker == "Angela" || speaker == "Child") {
			color = angela_color;
		} else if (speaker == "You") {
			color = you_color;
		} else if (speaker == "Z") {
			color = z_color;
		}

		// Draw the line
		y -= drawText(lineText(state, i), glm::vec2(position.x, y), state_width, triangle_strip, color);

		// Move down to create space for the next line
		y -= font_size;
	}
	return position.y - y;
}

void PlayMode::drawTimeline(const Timeline& timeline, std::vector<PPUDataStream::Vertex>* triangle_strip) {
	int x = timeline.index * timeline_width;
	int y = ScreenHeight;

	drawText("Year " + std::to_string(timeline.date), glm::vec2(x, y), timeline_width, triangle_strip, date_color);
	y -= font_size * 2;

	for (size_t i = 0; i < timeline.states.size(); i++) {
		if (scroll_to_timeline_end && timeline.index == observing_timeline && (i == 0 || observing_timeline == current_timeline)) {
			scroll_x = x - (ScreenWidth - timeline_width) / 2;
			scroll_y = y - ScreenHeight;
			if (i == 0) {
				scroll_y += font_size * 2;
			}
		}

		y -= drawState(timeline.states[i], glm::vec2(x, y), triangle_strip);
		y -= font_size;
	}

	drawText(std::to_string(timeline.index), glm::vec2(x - timeline_width * 0.05, scroll_y + (int)ScreenHeight), timeline_width, triangle_strip, timeline_index_color);
}

void PlayMode::drawTriangleStrip(const std::vector<PPUDataStream::Vertex>& triangle_strip) {
	// Upload vertex buffer
	glBindBuffer(GL_ARRAY_BUFFER, data_stream->vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(decltype(triangle_strip[0])) * triangle_strip.size(), triangle_strip.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set up the pipeline:
	// set blending function for output fragments:
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// set the shader programs:
	glUseProgram(tile_program->program);

	// configure attribute streams:
	glBindVertexArray(data_stream->vertex_buffer_for_tile_program);

	// set uniforms for shader programs:
	{ //set matrix to transform [0,ScreenWidth]x[0,ScreenHeight] -> [-1,1]x[-1,1]:
		//NOTE: glm uses column-major matrices:
		glm::mat4 OBJECT_TO_CLIP = glm::mat4(
			glm::vec4(2.0f / ScreenWidth, 0.0f, 0.0f, 0.0f),
			glm::vec4(0.0f, 2.0f / ScreenHeight, 0.0f, 0.0f),
			glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
			glm::vec4(-1.0f - scroll_x * 2.f / ScreenWidth, -1.0f - scroll_y * 2.f / ScreenHeight, 0.0f, 1.0f)
		);
		glUniformMatrix4fv(tile_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(OBJECT_TO_CLIP));
	}

	// bind texture units to proper texture objects:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, data_stream->tile_tex);

	//now that the pipeline is configured, trigger drawing of triangle strip:
	glDrawArrays(GL_TRIANGLE_STRIP, 0, GLsizei(triangle_strip.size()));

	GL_ERRORS();
}

void PlayMode::draw(glm::uvec2 const& drawable_size) {
	// Most of the code in this function borrows heavily from PPU466.cpp

	//this code does screen scaling by manipulating the viewport, so save old values:
	GLint old_viewport[4];
	glGetIntegerv(GL_VIEWPORT, old_viewport);

	//draw to whole drawable:
	glViewport(0, 0, drawable_size.x, drawable_size.y);

	//background gets background color:
	glClearColor(
		background_color.r / 255.0f,
		background_color.g / 255.0f,
		background_color.b / 255.0f,
		1.0f
	);
	glClear(GL_COLOR_BUFFER_BIT);

	//set up screen scaling:
	if (drawable_size.x < ScreenWidth || drawable_size.y < ScreenHeight) {
		//if screen is too small, just do some inglorious pixel-mushing:
		//(viewport is already set. nothing more to do.)
	}
	else {
		//otherwise, do careful integer-multiple upscaling:
		//largest size that will fit in the drawable:
		const uint32_t scale = std::max(1U, std::min(drawable_size.x / ScreenWidth, drawable_size.y / ScreenHeight));

		//compute lower left so that screen is centered:
		const glm::ivec2 lower_left = glm::ivec2(
			(int32_t(drawable_size.x) - scale * int32_t(ScreenWidth)) / 2,
			(int32_t(drawable_size.y) - scale * int32_t(ScreenHeight)) / 2
		);
		glViewport(lower_left.x, lower_left.y, scale * ScreenWidth, scale * ScreenHeight);
	}

	std::vector< PPUDataStream::Vertex > triangle_strip;

	triggers.clear();

	//drawState(states[current_state], glm::ivec2(0, ScreenHeight), &triangle_strip);
	for (size_t i = 0; i < timelines.size(); i ++) {
		drawTimeline(timelines[i], &triangle_strip);
	}

	scroll_to_timeline_end = false;

	drawTriangleStrip(triangle_strip);

	//return state to default:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glDisable(GL_BLEND);

	//also restore viewport, since earlier scaling code messed with it:
	glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);

	GL_ERRORS();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PlayMode::PPUTileProgram::PPUTileProgram() {
	program = gl_compile_program(
		//vertex shader:
		"#version 330\n"
		"uniform mat4 OBJECT_TO_CLIP;\n"
		"in vec4 Position;\n"
		"in ivec2 TileCoord;\n"
		"out vec2 tileCoord;\n"
		"in vec4 Color;\n"
		"out vec4 color;\n"
		"void main() {\n"
		"	gl_Position = OBJECT_TO_CLIP * Position;\n"
		"	tileCoord = TileCoord;\n"
		"	color = Color;\n"
		"}\n"
		,
		//fragment shader:
		"#version 330\n"
		"uniform sampler2D TILE_TABLE;\n"
		"in vec2 tileCoord;\n"
		"out vec4 fragColor;\n"
		"in vec4 color;\n"
		"void main() {\n"
		"fragColor = texelFetch(TILE_TABLE, ivec2(tileCoord), 0);\n"
		"fragColor.r = color.r;\n"
		"fragColor.g = color.g;\n"
		"fragColor.b = color.b;\n"
		"}\n"
	);

	//look up the locations of vertex attributes:
	Position_vec2 = glGetAttribLocation(program, "Position");
	TileCoord_ivec2 = glGetAttribLocation(program, "TileCoord");
	Color_vec4 = glGetAttribLocation(program, "Color");
	//Palette_int = glGetAttribLocation(program, "Palette");

	//look up the locations of uniforms:
	OBJECT_TO_CLIP_mat4 = glGetUniformLocation(program, "OBJECT_TO_CLIP");

	GLuint TILE_TABLE_usampler2D = glGetUniformLocation(program, "TILE_TABLE");
	//GLuint PALETTE_TABLE_sampler2D = glGetUniformLocation(program, "PALETTE_TABLE");

	//bind texture units indices to samplers:
	glUseProgram(program);
	glUniform1i(TILE_TABLE_usampler2D, 0);
	//glUniform1i(PALETTE_TABLE_sampler2D, 1);
	glUseProgram(0);

	GL_ERRORS();
}

PlayMode::PPUTileProgram::~PPUTileProgram() {
	if (program != 0) {
		glDeleteProgram(program);
		program = 0;
	}
}

//PPU data is streamed to the GPU (read: uploaded 'just in time') using a few buffers:
PlayMode::PPUDataStream::PPUDataStream() {

	//vertex_buffer_for_tile_program is a vertex array object that tells the GPU the layout of data in vertex_buffer:
	glGenVertexArrays(1, &vertex_buffer_for_tile_program);
	glBindVertexArray(vertex_buffer_for_tile_program);

	//vertex_buffer will (eventually) hold vertex data for drawing:
	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

	//Notice how this binding is attaching an integer input to a floating point attribute:
	glVertexAttribPointer(
		tile_program->Position_vec2, //attribute
		2, //size
		GL_INT, //type
		GL_FALSE, //normalized
		sizeof(Vertex), //stride
		(GLbyte*)0 + offsetof(Vertex, Position) //offset
	);
	glEnableVertexAttribArray(tile_program->Position_vec2);

	//the "I" variant binds to an integer attribute:
	glVertexAttribIPointer(
		tile_program->TileCoord_ivec2, //attribute
		2, //size
		GL_INT, //type
		sizeof(Vertex), //stride
		(GLbyte*)0 + offsetof(Vertex, TileCoord) //offset
	);
	glEnableVertexAttribArray(tile_program->TileCoord_ivec2);

	// Add color attribute
	glVertexAttribPointer(
		tile_program->Color_vec4, //attribute
		4, //size
		GL_FLOAT, //type
		GL_FALSE, //normalized
		sizeof(Vertex), //stride
		(GLbyte*)0 + offsetof(Vertex, Color) //offset
	);
	glEnableVertexAttribArray(tile_program->Color_vec4);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	glGenTextures(1, &tile_tex);
	glBindTexture(GL_TEXTURE_2D, tile_tex);
	//passing 'nullptr' to TexImage says "allocate memory but don't store anything there":
	// (textures will be uploaded later)
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 26, 26, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	//make the texture have sharp pixels when magnified:
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	//when access past the edge, clamp to the edge:
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	GL_ERRORS();
}

PlayMode::PPUDataStream::~PPUDataStream() {
	if (vertex_buffer_for_tile_program != 0) {
		glDeleteVertexArrays(1, &vertex_buffer_for_tile_program);
		vertex_buffer_for_tile_program = 0;
	}
	if (vertex_buffer != 0) {
		glDeleteBuffers(1, &vertex_buffer);
		vertex_buffer = 0;
	}
	if (tile_tex != 0) {
		glDeleteTextures(1, &tile_tex);
		tile_tex = 0;
	}
}
