#include <itu_engine.hpp>

const float GRAVITY = -9.8f;

const float ball_radius          = 0.4f;
const float launcher_width       = 1.0f;
const float launcher_height      = 3.0f;   // height of the launcher when it is not charged
const float launcher_compression = 1.8f;   // how much the launcher shrinks at maximum charge
const float launcher_margin      = 1.0f;   // distance between the launcher center and the right window edge
const float launcher_bottom_y    = -8.0f;  // fixed bottom of the launcher (world space)

const float power_min    = 5.0f;
const float power_max    = 40.0f;
const float charge_speed = 20.0f;          // power gained per second while E is held

enum LauncherPhase
{
	LAUNCHER_PHASE_READY,        // ball rests on the launcher
	LAUNCHER_PHASE_BALL_IN_PLAY  // ball has been launched
};

struct PinballGameState
{
	LauncherPhase phase;
	bool  charging;
	float power;
	bool  request_reset;

	vec2f    ball_position;
	b2BodyId ball_body_id;

	b2WorldId world_id;
};



// visible world bounds
static void get_world_bounds(EngineContext* context, vec2f* out_min, vec2f* out_max)
{
	Camera* camera = context->camera_active;
	vec2f half = vec2f { context->sdl_window_w, context->sdl_window_h } / (camera->pixels_per_unit * camera->zoom * 2);
	*out_min = camera->world_position - half;
	*out_max = camera->world_position + half;
}

static float launcher_x(EngineContext* context)
{
	vec2f bmin, bmax;
	get_world_bounds(context, &bmin, &bmax);
	return bmax.x - launcher_margin;
}

// 0 = not charged, 1 = fully charged
static float launcher_charge_t(PinballGameState* state)
{
	return (state->power - power_min) / (power_max - power_min);
}

static float launcher_current_height(PinballGameState* state)
{
	return launcher_height - launcher_charge_t(state) * launcher_compression;
}

// ball center position while it rests on top of the launcher
static vec2f ball_rest_position(EngineContext* context, PinballGameState* state)
{
	float top_y = launcher_bottom_y + launcher_current_height(state);
	return vec2f { launcher_x(context), top_y + ball_radius };
}




static void game_init(EngineContext* context, PinballGameState* state)
{
	itu_lib_input_set_mapping_keyboard(context, SDLK_E, BTN_TYPE_ACTION_0);

	itu_lib_input_set_mapping_keyboard(context, SDLK_F1, BTN_TYPE_DEBUG_F1);
	itu_lib_input_set_mapping_keyboard(context, SDLK_TAB, BTN_TYPE_DEBUG_RESET);

	state->world_id = { 0 };
}

static void game_reset(EngineContext* context, PinballGameState* state)
{
	if(b2World_IsValid(state->world_id))
		b2DestroyWorld(state->world_id);

	b2WorldDef def_world = b2DefaultWorldDef();
	def_world.gravity.y = GRAVITY;
	state->world_id = b2CreateWorld(&def_world);

	state->phase = LAUNCHER_PHASE_READY;
	state->charging = false;
	state->power = power_min;
	state->request_reset = false;
	state->ball_position = ball_rest_position(context, state);

	// ball: dynamic body, disabled while it rests on the launcher
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.type      = b2_dynamicBody;
	body_def.position  = value_cast(b2Vec2, state->ball_position);
	body_def.isEnabled = false;

	b2ShapeDef shape_def = b2DefaultShapeDef();
	shape_def.density = 1;

	b2Circle circle = { { 0, 0 }, ball_radius };

	state->ball_body_id = b2CreateBody(state->world_id, &body_def);
	b2CreateCircleShape(state->ball_body_id, &shape_def, &circle);
}



static void game_update(EngineContext* context, PinballGameState* state)
{
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_RESET] || state->request_reset)
		game_reset(context, state);

	if(state->phase != LAUNCHER_PHASE_READY)
		return;

	bool key_down = context->btn_isdown[BTN_TYPE_ACTION_0];

	if(key_down)
	{
		// charging
		state->charging = true;
		state->power += charge_speed * context->delta;
		state->power = SDL_clamp(state->power, power_min, power_max);

		// the ball follows the top of the launcher
		state->ball_position = ball_rest_position(context, state);
	}
	else if(state->charging)
	{
		// key released -> launch
		b2Body_Enable(state->ball_body_id);
		b2Body_SetTransform(state->ball_body_id, value_cast(b2Vec2, state->ball_position), b2MakeRot(0));
		b2Body_SetLinearVelocity(state->ball_body_id, b2Vec2{ 0, state->power });
		b2Body_SetAngularVelocity(state->ball_body_id, 0);

		state->phase    = LAUNCHER_PHASE_BALL_IN_PLAY;
		state->charging = false;
		state->power    = power_min; // the spring goes back to its resting state
	}
}

static void game_update_post_physics(EngineContext* context, PinballGameState* state)
{
	if(state->phase != LAUNCHER_PHASE_BALL_IN_PLAY)
		return;

	b2Vec2 pos = b2Body_GetPosition(state->ball_body_id);
	state->ball_position = value_cast(vec2f, pos);

	// ball outside the window -> reset
	vec2f bmin, bmax;
	get_world_bounds(context, &bmin, &bmax);
	float r = ball_radius;
	if(pos.x < bmin.x - r || pos.x > bmax.x + r || pos.y < bmin.y - r || pos.y > bmax.y + r)
		state->request_reset = true;
}



static void game_render(EngineContext* context, PinballGameState* state)
{
	itu_lib_render_draw_world_grid(context);

	// launcher: (x, y) in world space
	{
		float height = launcher_current_height(state);
		SDL_FRect world_rect;
		world_rect.x = launcher_x(context) - launcher_width / 2;
		world_rect.y = launcher_bottom_y;
		world_rect.w = launcher_width;
		world_rect.h = height;

		SDL_FRect r = itu_lib_context_rect_global_to_screen(context, world_rect);
		itu_lib_render_screen_rect_fill(context->renderer, vec2f { r.x, r.y }, vec2f { r.w, r.h }, color { 0.7f, 0.7f, 0.8f, 1.0f });
	}

	// ball (filled polygon for now)
	{
		const int vertex_count = 12;
		float radius_px = itu_lib_context_size_global_to_screen(context, ball_radius);
		vec2f vertices[vertex_count];
		for(int i = 0; i < vertex_count; ++i)
		{
			float angle = TAU * i / vertex_count;
			vertices[i] = vec2f { radius_px * SDL_cosf(angle), radius_px * SDL_sinf(angle) };
		}
		itu_lib_render_draw_world_polygon(context, state->ball_position, vertices, vertex_count, COLOR_YELLOW);
	}

	// debug UI
	ImGui::Begin("launcher");
	ImGui::Text("phase : %s", state->phase == LAUNCHER_PHASE_READY ? "READY" : "BALL_IN_PLAY");
	ImGui::Text("power : %.2f", state->power);
	ImGui::ProgressBar(launcher_charge_t(state));
	ImGui::End();
}




int main(void)
{
	bool quit = false;
	EngineConfig config = EngineConfig();
	EngineContext context = { 0 };
	PinballGameState state = { };

	config.application_name = "Pinball Machine";
	config.step_per_second_fixed = 60;

	itu_lib_context_init(&config, &context);
	itu_lib_imgui_setup(&context, true);
	itu_lib_context_set_active_camera(&context, &context.camera_default);

	game_init(&context, &state);
	game_reset(&context, &state);

	itu_lib_context_frame_timing_setup(&context);

	while(!quit)
	{
		quit = itu_lib_input_process_events(&context);

		if(context.btn_isjustpressed[BTN_TYPE_DEBUG_F1])
			context.debug_ui_show = !context.debug_ui_show;

		SDL_SetRenderDrawColor(context.renderer, 0x00, 0x00, 0x00, 0x00);
		SDL_RenderClear(context.renderer);

		itu_lib_imgui_frame_begin(&context);

		game_update(&context, &state);

		// physics (fixed step)
		{
			context.physics_steps_per_frame_count = 0;
			while(context.accumulator_physics >= context.target_framerate_fixed_ns && context.physics_steps_per_frame_count < config.physics_steps_per_frame_max)
			{
				b2World_Step(state.world_id, NS_TO_SECONDS(context.target_framerate_fixed_ns), 4);
				++context.physics_steps_per_frame_count;
				context.accumulator_physics -= context.target_framerate_fixed_ns;

				game_update_post_physics(&context, &state);
			}
		}

		game_render(&context, &state);

		itu_lib_imgui_frame_end(&context);
		SDL_RenderPresent(context.renderer);
		itu_lib_context_frame_timing_update(&context);
	}
}