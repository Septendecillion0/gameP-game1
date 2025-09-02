#include "PlayMode.hpp"

//for the GL_ERRORS() macro:
#include "gl_errors.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>

#include "Load.hpp"
#include "data_path.hpp"
#include "read_write_chunk.hpp"
#include <random>
#include <fstream>
#include <stdexcept>
#include <deque>


//heavily assisted by ChatGPT

// global streams for asset files
std::ifstream palette_stream;
std::ifstream tile_stream;

/*
//random seed setup
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<int> dist(1, 100);
*/

//init (note: could be moved to an init function for organization)
bool player_live = true;

bool holding_brick = false;

struct Brick {
    float x_pos;
    float y_pos;
    bool held;
    bool active;

    Brick(float x = 0.0f, float y = 0.0f, bool h = false)
        : x_pos(x), y_pos(y), held(h), active(true) {}
};
std::deque<Brick> bricks;

int PlayerYDirection = 1;
float brickTimer = 0.0f;


// use Load<> to open the files after OpenGL context is ready
Load<void> ps(LoadTagDefault, []() {
    palette_stream.open(data_path("../assets/palettes.asset"), std::ios::binary);
    if (!palette_stream.is_open()) {
        throw std::runtime_error("Failed to open palettes.asset");
    }

    tile_stream.open(data_path("../assets/tiles.asset"), std::ios::binary);
    if (!tile_stream.is_open()) {
        throw std::runtime_error("Failed to open tiles.asset");
    }
});


PlayMode::PlayMode() {
    // --- load tiles and palettes from files ---
    std::vector<PPU466::Palette> palettes;
    std::vector<PPU466::Tile> tiles;

    read_chunk(palette_stream, std::string("pale"), &palettes);
    read_chunk(tile_stream, std::string("tile"), &tiles);

    palette_stream.close();
    tile_stream.close();

    std::cout << "Tiles num = " << tiles.size() << std::endl;

    int tile_start = 32; // player sprite start
    for (size_t i = 0; i < tiles.size(); ++i) {
        ppu.tile_table[tile_start + i] = tiles[i];
    }

    // palettes: assign only first N
    for (size_t i = 0; i < palettes.size() && i < 7; ++i) {
        ppu.palette_table[i + 1] = palettes[i];
    }

    // create a blank tile
	PPU466::Tile blank_tile;
	blank_tile.bit0.fill(0);
	blank_tile.bit1.fill(0);
	ppu.tile_table[0] = blank_tile;

	// fill background with tile 0
	for (auto &bg : ppu.background) {
		bg = 0; // tile index 0, palette 0
	}

    // clear background tilemap (so no repeating palette blocks)
    for (auto &bg : ppu.background) {
        bg = 0; // use tile 0 and palette 0
    }
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_LEFT) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_RIGHT) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_UP) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_DOWN) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_LEFT) {
			left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_RIGHT) {
			right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_UP) {
			up.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_DOWN) {
			down.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {


	constexpr float PlayerSpeed = 80.0f;
	constexpr float PlayerYSpeed = 30.0f;
	float PlayerYVel = PlayerYSpeed * PlayerYDirection;
	constexpr float UpperBound = 210.0f; //screen height 240px, lilguy is 30 tall
	constexpr float LowerBound = 140.0f;
	constexpr float BrickSpeed = 120.0f;
	constexpr float BrickCooldown = 0.2f;

	brickTimer = std::max(0.0f, brickTimer - elapsed);
	/*
	if (left.pressed) player_at.x -= PlayerSpeed * elapsed;
	if (right.pressed) player_at.x += PlayerSpeed * elapsed;
	if (down.pressed) player_at.y -= PlayerSpeed * elapsed;
	if (up.pressed) player_at.y += PlayerSpeed * elapsed;
	*/

	//move brick
	if (!bricks.empty() && bricks.back().held) {
		if (left.pressed) bricks.back().x_pos = std::max(0.0f, bricks.back().x_pos - BrickSpeed * elapsed);
		if (right.pressed) bricks.back().x_pos = std::min(240.0f, bricks.back().x_pos + BrickSpeed * elapsed);
	}
	//note: screen width is 256px (240 - brick width)

	//throw brick
	if (up.pressed && holding_brick && !bricks.empty() && brickTimer == 0.0f) {
		holding_brick = false;
		bricks.back().held = false;
		brickTimer = BrickCooldown;
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;

	//update all brick pos
	for (Brick &brick : bricks) {
		if (brick.held) continue;
		brick.y_pos += BrickSpeed * elapsed;
		if (brick.y_pos > 240.0f) brick.active = false;
	}

	//update lilguy (player) pos
	player_at.x += PlayerSpeed * elapsed;
	float expected_player_y = player_at.y + PlayerYVel * elapsed;
	if (expected_player_y > UpperBound) {
		expected_player_y = (UpperBound - expected_player_y) + UpperBound;
		PlayerYDirection = -1;
	}
	else if (expected_player_y < LowerBound) {
		expected_player_y = (LowerBound - expected_player_y) + LowerBound;
		PlayerYDirection = 1;
	}
	player_at.y = expected_player_y;

	//check collision
	for (Brick &brick : bricks) {
		//player loops smoothly, so take mod of 256
		float x_diff = fmod(player_at.x, 256.0f) - brick.x_pos;
		bool x_collision = (x_diff > -20.0f) && (x_diff < 16.0f);
		float y_diff = player_at.y - brick.y_pos;
		bool y_collision = (y_diff > -16.0f) && (y_diff < 30.0f);

		if (x_collision && y_collision) {
			player_live = false;
			brick.active = false;
			break;
		}
	}

	//spawn a brick
	if (!holding_brick && brickTimer == 0.0f) {
		Brick new_brick =  Brick(100.0f, 0.0f, true);
		bricks.push_back(new_brick);
		holding_brick = true;
	}

	//despawn bricks (only check oldest)
	if (!bricks[0].active) {
		bricks.pop_front();
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {

    // --- clear all sprites off-screen ---
    for(auto &sprite : ppu.sprites) sprite.y = 240; // offscreen

    int slot = 0;

	constexpr int player_tile_start = 32;
	constexpr int player_cols = 3; // 20px / 8px → rounded up
	constexpr int player_rows = 4; // 30px / 8px → rounded up

	// --- draw player sprite ---
	if (player_live) {
		

		for(int ty = player_rows - 1; ty >= 0; --ty){
			for(int tx = 0; tx < player_cols; ++tx){
				ppu.sprites[slot].x = int8_t(player_at.x + tx*8);
				ppu.sprites[slot].y = int8_t(player_at.y + (player_rows - 1 - ty)*8);
				ppu.sprites[slot].index = uint8_t(player_tile_start + ty*player_cols + tx);
				ppu.sprites[slot].attributes = 1; // palette 1 for player
				++slot;
			}
		}
	}

    // --- draw one big brick ---

	constexpr int brick_tile_start = player_tile_start + player_cols*player_rows; // after lilguy
	constexpr int brick_cols = 2; // 16px / 8px
	constexpr int brick_rows = 2;

	for (Brick &brick : bricks) {
		for(int ty = 0; ty < brick_rows; ++ty){    // top-to-bottom in asset
			for(int tx = 0; tx < brick_cols; ++tx){ // left-to-right
				ppu.sprites[slot].x = int8_t(brick.x_pos + tx*8);
				ppu.sprites[slot].y = int8_t(brick.y_pos + (brick_rows - 1 - ty)*8);
				ppu.sprites[slot].index = uint8_t(brick_tile_start + ty*brick_cols + tx);
				ppu.sprites[slot].attributes = 2; // palette 2
				++slot;
			}
		}
	}

	

    // --- set background color to white ---
    ppu.background_color = glm::u8vec3(255,255,255);

    // --- draw ---
    ppu.draw(drawable_size);
}



