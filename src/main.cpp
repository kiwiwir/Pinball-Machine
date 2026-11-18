#include <itu_engine.hpp>
#include <gameObjectsLib.hpp>

const float GRAVITY = -9.8f;

const int COLLISION_FILTER_PLAYER         = 0b00001;
const int COLLISION_FILTER_SCORE_OBSTACLE = 0b00010;
const int COLLISION_FILTER_OBSTACLE		  = 0b00100;
const int ENTITY_COUNT = 1000;
const float ball_radius          = 0.4f;
const float launcher_width       = 1.0f;
const float launcher_height      = 3.0f;   // height of the launcher when it is not charged
const float launcher_compression = 1.8f;   // how much the launcher shrinks at maximum charge
const float launcher_margin      = 1.0f;   // distance between the launcher center and the right window edge
const float launcher_bottom_y    = -8.0f;  // fixed bottom of the launcher (world space)

const float power_min    = 5.0f;
const float power_max    = 40.0f;
const float charge_speed = 20.0f;          // power gained per second while E is held

bool DEBUG_render_textures = true;
bool DEBUG_render_outlines = false;
bool DEBUG_physics = true;
int DEBUG_simulation_type_current = 0;

b2DebugDraw debug_draw;


enum LauncherPhase
{
	LAUNCHER_PHASE_READY,        // ball rests on the launcher
	LAUNCHER_PHASE_BALL_IN_PLAY  // ball has been launched
};

bool operator<(const b2BodyId& lhs, const b2BodyId& rhs)
{
    return lhs.index1 < rhs.index1;
}

bool operator==(const b2ShapeId& lhs, const b2ShapeId& rhs)
{
    return lhs.index1 == rhs.index1;
}

struct Entity
{
	Sprite      sprite;
	Transform2D transform;
	b2BodyId    body_id;
	vec2f       velocity;
};

struct PinballGameState
{
	LauncherPhase phase;
	bool  charging;
	float power;
	bool  request_reset;

	vec2f    ball_position;
	b2BodyId ball_body_id;

	b2BodyId* flipper_left_id;
	b2BodyId* flipper_right_id;

	b2WorldId world_id;
	SDL_Texture* atlas;

	map<b2BodyId, Entity*> entities;
	list<b2ShapeId> shape_ids;
	int entities_alive_count;
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


static void entity_create(PinballGameState* state, Entity * entity, b2BodyId bodyId)
{
	if(!(state->entities_alive_count < ENTITY_COUNT))
		// NOTE: this might as well be an assert, if we don't have a way to recover/handle it
		return;

	// // concise version
	//return &state->entities[state->entities_alive_count++];

	state->entities[bodyId] = entity;
	++state->entities_alive_count;
	return;
}

// NOTE: this only works if nobody holds references to other entities!
//       if that were the case, we couldn't swap them around.
//       We will see in later lectures how to handle this kind of problems
static void entity_destroy(PinballGameState* state, b2BodyId bodyId)
{
	// NOTE: here we want to fail hard, nobody should pass us a pointer not gotten from `entity_create()`
	list<b2ShapeId>::iterator it = state->shape_ids.begin();
	
	while (it != state->shape_ids.end()){
		b2ShapeId shape = *it;
		
		int32_t index =	b2Shape_GetBody(shape).index1;
		if(index == bodyId.index1){
			SDL_Log("removing shape: %d", shape.index1);
			it = state->shape_ids.erase(it);
			b2DestroyShape(shape, false);
			continue;
		}
		it++;
	}
	SDL_Log("removing body: %d", bodyId.index1);
	state->entities.erase(bodyId);
	b2DestroyBody(bodyId);
	--state->entities_alive_count;
}

static void game_init(EngineContext* context, PinballGameState* state)
{
	itu_lib_input_set_mapping_keyboard(context, SDLK_A, BTN_TYPE_LEFT);
	itu_lib_input_set_mapping_keyboard(context, SDLK_D, BTN_TYPE_RIGHT);
	itu_lib_input_set_mapping_keyboard(context, SDLK_SPACE, BTN_TYPE_ACTION_0);

	itu_lib_input_set_mapping_keyboard(context, SDLK_F1, BTN_TYPE_DEBUG_F1);
	itu_lib_input_set_mapping_keyboard(context, SDLK_TAB, BTN_TYPE_DEBUG_RESET);
	
	state->entities = {};

	state->atlas = itu_resources_texture_create(context, "data/kenney/tiny_dungeon_packed.png", SDL_SCALEMODE_NEAREST);

	state->world_id = { 0 };
}

static void game_reset(EngineContext* context, PinballGameState* state)
{
	if(b2World_IsValid(state->world_id))
		b2DestroyWorld(state->world_id);

	b2WorldDef def_world = b2DefaultWorldDef();
	def_world.gravity.y = GRAVITY;
	state->world_id = b2CreateWorld(&def_world);
	b2World_SetHitEventThreshold(state->world_id, 0);

	state->phase = LAUNCHER_PHASE_READY;
	state->charging = false;
	state->power = power_min;
	state->request_reset = false;
	state->ball_position = ball_rest_position(context, state);

	// launcher:
	b2BodyDef launcher_body_def = b2DefaultBodyDef();
	launcher_body_def.type      = b2_kinematicBody;
	launcher_body_def.position  = {5.5, -6};
	
	b2ShapeDef launcher_shape_def = b2DefaultShapeDef();
	launcher_shape_def.density = 1;

	Entity* launcher_entity = new Entity();
	launcher_entity->transform.scale = vec2f{1, 1};
	launcher_entity->transform.rotation = 0;

	b2Polygon launcher_polygon = b2MakeBox(0.5f, 2.0f);
	
	launcher_entity->body_id = b2CreateBody(state->world_id, &launcher_body_def);

	b2CreatePolygonShape(launcher_entity->body_id, &launcher_shape_def, &launcher_polygon);

	// ball: dynamic body, disabled while it rests on the launcher
	b2BodyDef body_def = b2DefaultBodyDef();
	body_def.type      = b2_dynamicBody;
	body_def.position  = value_cast(b2Vec2, state->ball_position);
	body_def.isEnabled = true;

	b2ShapeDef shape_def = b2DefaultShapeDef();
	shape_def.density = 1;

	Entity* ball_entity = new Entity();
	ball_entity->transform.scale = vec2f{1, 1};
	ball_entity->transform.rotation = 0;
	vec2f size = itu_lib_sprite_get_world_size(context, &ball_entity->sprite, &ball_entity->transform);
	vec2f offset = -mul_element_wise(size, ball_entity->sprite.pivot - vec2f{ 0.5f, 0.5f });


	b2Circle circle = { { 0, 0 }, ball_radius };

	
	circle.center = value_cast(b2Vec2, offset);
	body_def.position = b2Vec2{-2,0};
	ball_entity->body_id = b2CreateBody(state->world_id, &body_def);

	state->ball_body_id = b2CreateBody(state->world_id, &body_def);
	//b2CreateCircleShape(state->ball_body_id, &shape_def, &circle);
	
	shape_def.density = 1;
	shape_def.enableHitEvents = true;
	shape_def.enableContactEvents = true;
	shape_def.filter.categoryBits = COLLISION_FILTER_PLAYER;
	shape_def.filter.maskBits     = COLLISION_FILTER_OBSTACLE | COLLISION_FILTER_SCORE_OBSTACLE;

	entity_create(state, ball_entity, ball_entity->body_id);

	b2CreateCircleShape(ball_entity->body_id, &shape_def, &circle);

	itu_lib_sprite_init(
		&ball_entity->sprite,
		state->atlas,
		itu_lib_sprite_get_source_rect(7, 7, 16, 16)
	);	

	//SphereObject

	{
		b2BodyDef sphere_body_def = b2DefaultBodyDef();
		sphere_body_def.type = b2_staticBody;
		
		b2ShapeDef sphere_shape_def = b2DefaultShapeDef();
		//sphere_shape_def.isSensor = true;
		//sphere_shape_def.enableSensorEvents = true;

		sphere_shape_def.density = 1;
		sphere_shape_def.enableHitEvents = true;
		sphere_shape_def.enableContactEvents = true;
		sphere_shape_def.filter.categoryBits = COLLISION_FILTER_SCORE_OBSTACLE;
		sphere_shape_def.filter.maskBits     = COLLISION_FILTER_PLAYER;

		Entity* sphere_entity = new Entity();

		sphere_entity->transform.scale = vec2f{2, 2};
		sphere_entity->transform.rotation = 0;

		vec2f size = itu_lib_sprite_get_world_size(context, &sphere_entity->sprite, &sphere_entity->transform);
		vec2f offset = -mul_element_wise(size, sphere_entity->sprite.pivot - vec2f{ 0.5f, 0.5f });

		b2Circle sphere_circle;
		sphere_circle.radius = 1.0f;
		sphere_circle.center = value_cast(b2Vec2, offset);
		sphere_body_def.position = b2Vec2{0,0};
		sphere_entity->body_id = b2CreateBody(state->world_id, &sphere_body_def);
		
		entity_create(state, sphere_entity, sphere_entity->body_id);

		b2CreateCircleShape(sphere_entity->body_id, &sphere_shape_def, &sphere_circle);

		itu_lib_sprite_init(
			&sphere_entity->sprite,
			state->atlas,
			itu_lib_sprite_get_source_rect(7, 7, 16, 16)
		);		
	}

	// walls

    b2BodyDef  wall_body_def  = b2DefaultBodyDef();
    b2ShapeDef wall_shape_def = b2DefaultShapeDef();
    b2Polygon  wall_horizontal = b2MakeBox(5.5f, 0.1f);
	b2Polygon  wall_between_launcher   = b2MakeBox(0.1f, 7.0f);
    b2Polygon  wall_vertical   = b2MakeBox(0.1f, 8.0f);
    b2Polygon  wall_inside     = b2MakeBox(1.5f, 0.1f);
	b2Polygon  wall_corner     = b2MakeBox(1.0f, 0.1f);

	
	wall_shape_def.enableHitEvents = true;
	wall_shape_def.enableContactEvents = true;
	wall_shape_def.filter.categoryBits = COLLISION_FILTER_OBSTACLE;
	wall_shape_def.filter.maskBits     = COLLISION_FILTER_PLAYER;

    wall_body_def.position = b2Vec2{0.5f, 8.0f};
    b2BodyId wall_top_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_top_id, &wall_shape_def, &wall_horizontal);

    wall_body_def.position = b2Vec2{0.5f, -8.0f};
    b2BodyId wall_bottom_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_bottom_id, &wall_shape_def, &wall_horizontal);

    wall_body_def.position = b2Vec2{-5.0f, 0.0f};
    b2BodyId wall_left_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_left_id, &wall_shape_def, &wall_vertical);

	wall_body_def.position = b2Vec2{5.0f, -1.0f};
    b2BodyId wall_between_launcher_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_between_launcher_id, &wall_shape_def, &wall_between_launcher);

    wall_body_def.position = b2Vec2{6.0f, 0.0f};
    b2BodyId wall_right_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_right_id, &wall_shape_def, &wall_vertical);
	
	wall_body_def.position = b2Vec2{5.5f, 7.5f};
	wall_body_def.rotation = b2MakeRot(-DEG_2_RAD * 45.0f);
    b2BodyId wall_corner_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_corner_id, &wall_shape_def, &wall_corner);

    wall_body_def.position = b2Vec2{-4.0f, -3.0f};
    wall_body_def.rotation = b2MakeRot(-0.7f);
    b2BodyId wall_inside_left_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_inside_left_id, &wall_shape_def, &wall_inside);

    wall_body_def.position = b2Vec2{4.0f, -3.0f};
    wall_body_def.rotation = b2MakeRot(0.7f);
    b2BodyId wall_inside_right_id = b2CreateBody(state->world_id, &wall_body_def);
    b2CreatePolygonShape(wall_inside_right_id, &wall_shape_def, &wall_inside);
	
    // flippers
    
    b2BodyDef  flipper_body_def  = b2DefaultBodyDef();
    b2ShapeDef flipper_shape_def = b2DefaultShapeDef();
    b2Polygon  flipper = b2MakeBox(1.0f, 0.1f);
	Entity*    flipper_left_entity  = new Entity();
	Entity*    flipper_right_entity = new Entity();

	flipper_shape_def.enableHitEvents = true;
	flipper_shape_def.enableContactEvents = true;
	flipper_shape_def.filter.categoryBits = COLLISION_FILTER_OBSTACLE;
	flipper_shape_def.filter.maskBits     = COLLISION_FILTER_PLAYER;

    flipper_body_def.type = b2_dynamicBody;
    flipper_shape_def.density = 1.0f;
    flipper_shape_def.material.friction = 0.0f;

    flipper_body_def.position = b2Vec2{-2.2f, -4.5f};
    flipper_body_def.rotation = b2MakeRot(-0.7f);
    flipper_left_entity->body_id = b2CreateBody(state->world_id, &flipper_body_def);
    b2CreatePolygonShape(flipper_left_entity->body_id, &flipper_shape_def, &flipper);
	entity_create(state, flipper_left_entity, flipper_left_entity->body_id);
	state->flipper_left_id = &flipper_left_entity->body_id;
	
    flipper_body_def.position = b2Vec2{2.2f, -4.5f};
    flipper_body_def.rotation = b2MakeRot(0.7f);
    flipper_right_entity->body_id = b2CreateBody(state->world_id, &flipper_body_def);
    b2CreatePolygonShape(flipper_right_entity->body_id, &flipper_shape_def, &flipper);
	entity_create(state, flipper_right_entity, flipper_right_entity->body_id);
	state->flipper_right_id = &flipper_right_entity->body_id;

    // joints
	
    b2RevoluteJointDef joint_def = b2DefaultRevoluteJointDef();
    joint_def.enableLimit = true;

    joint_def.bodyIdA = wall_inside_left_id;
    joint_def.bodyIdB = flipper_left_entity->body_id;
    joint_def.localAnchorA = {1.5f, 0.0f};
    joint_def.localAnchorB = {-1.0f, 0.0f};
    joint_def.lowerAngle = 0.0f;
    joint_def.upperAngle = 1.5f;
    b2JointId jointLeftId = b2CreateRevoluteJoint(state->world_id, &joint_def);

    joint_def.bodyIdA = wall_inside_right_id;
    joint_def.bodyIdB = flipper_right_entity->body_id;
    joint_def.localAnchorA = {-1.5f, 0.0f};
    joint_def.localAnchorB = {1.0f, 0.0f};
    joint_def.lowerAngle = -1.5f;
    joint_def.upperAngle = 0.0f;
    b2JointId jointRightId = b2CreateRevoluteJoint(state->world_id, &joint_def);

	debug_draw.context = context;
	debug_draw.drawShapes = true;
	debug_draw.DrawSolidPolygonFcn = fn_box2d_wrapper_draw_polygon;
	debug_draw.DrawSolidCircleFcn  = fn_box2d_wrapper_draw_circle;
}



static void game_update(EngineContext* context, PinballGameState* state)
{
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_RESET] || state->request_reset)
		game_reset(context, state);

	if(state->phase != LAUNCHER_PHASE_READY)
		return;

	bool key_down = context->btn_isdown[BTN_TYPE_ACTION_0];

	if(context->btn_isdown[BTN_TYPE_LEFT])
		b2Body_ApplyAngularImpulse(*state->flipper_left_id, 5.0f, true);

	if(context->btn_isdown[BTN_TYPE_RIGHT])
		b2Body_ApplyAngularImpulse(*state->flipper_right_id, -5.0f, true);

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
	// NOTE: we are compouding precision errors here (config specifies frequency in steps per second, we convert to period in nanos,
	//       and here we convert back to seconds), but specifying what you want once and expressing everything else in function of
	//       it avoids mismatching. A more advanced implementation would have independent loop frequencies for game and physics and
	//       synch them under the hood (you can try it in exercise 04.2, will be discussed in class during exercise review)
	b2World_Step(state->world_id, NS_TO_SECONDS(context->target_framerate_fixed_ns), 4);

	


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

	// sphere obstacle (filled polygon for now)
	{
		for (auto pair : state->entities)
		{
			pair.second->transform;
			const int vertex_count = 12;
			float radius_px = itu_lib_context_size_global_to_screen(context, ball_radius);
			vec2f vertices[vertex_count];
			for(int i = 0; i < vertex_count; ++i)
			{
				float angle = TAU * i / vertex_count;
				vertices[i] = vec2f { radius_px * SDL_cosf(angle), radius_px * SDL_sinf(angle) };
			}
			itu_lib_render_draw_world_polygon(context, pair.second->transform.position, vertices, vertex_count, COLOR_BLUE);
		}
	}

	// debug UI
	ImGui::Begin("launcher");
	ImGui::Text("phase : %s", state->phase == LAUNCHER_PHASE_READY ? "READY" : "BALL_IN_PLAY");
	ImGui::Text("power : %.2f", state->power);
	ImGui::ProgressBar(launcher_charge_t(state));
	ImGui::End();
}

static void game_render_2(EngineContext* context, PinballGameState* state)
{
    itu_lib_render_draw_world_grid(context);

    if(context->btn_isjustpressed[BTN_TYPE_DEBUG_F1]) DEBUG_render_textures = !DEBUG_render_textures;
    if(context->btn_isjustpressed[BTN_TYPE_DEBUG_F2]) DEBUG_render_outlines = !DEBUG_render_outlines;
    if(context->btn_isjustpressed[BTN_TYPE_DEBUG_F3]) DEBUG_physics = !DEBUG_physics;

    // entities
    for(auto pair : state->entities)
    {
        Entity * entity = pair.second;
        // render texture
        SDL_FRect rect_src = entity->sprite.rect;	
        SDL_FRect rect_dst;

        if(DEBUG_render_textures)
            itu_lib_sprite_render(context, &entity->sprite, &entity->transform);

        if(DEBUG_render_outlines)
            itu_lib_sprite_render_debug(context, &entity->sprite, &entity->transform);
    }

    if(DEBUG_physics)
        b2World_Draw(state->world_id, &debug_draw);

    // debug window
    itu_lib_render_draw_world_point(context, VEC2F_ZERO, 10, color { 1, 0, 1, 1 });

    SDL_SetRenderDrawColor(context->renderer, 0xFF, 0x00, 0xFF, 0xff);
    SDL_RenderRect(context->renderer, NULL);
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

		//game_render(&context, &state);
		game_render_2(&context, &state);
		itu_lib_imgui_frame_end(&context);
		SDL_RenderPresent(context.renderer);
		itu_lib_context_frame_timing_update(&context);
	}
}