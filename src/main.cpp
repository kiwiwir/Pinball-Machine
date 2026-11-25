#include <itu_engine.hpp>
#include <gameObjectsLib.hpp>
#include <optional>
#include <algorithm>

const float GRAVITY = -9.8f;

const int COLLISION_FILTER_PLAYER         = 0b00001;
const int COLLISION_FILTER_SCORE_OBSTACLE = 0b00010;
const int COLLISION_FILTER_OBSTACLE		  = 0b00100;
const int COLLISION_KILLER				  = 0b01000;

const int ENTITY_COUNT = 1000;
const float ball_radius          = 0.4f;
/*const float launcher_width       = 1.0f;
const float launcher_height      = 3.0f;   // height of the launcher when it is not charged
const float launcher_compression = 1.8f;   // how much the launcher shrinks at maximum charge
const float launcher_margin      = 1.0f;   // distance between the launcher center and the right window edge
const float launcher_bottom_y    = -8.0f;  // fixed bottom of the launcher (world space)*/

const vec2f LAUNCHER_START_POS = {5.5, -6};


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
	b2BodyId* ball_body_id;

	b2BodyId launcher_body_id;
	b2BodyId flipper_left_id;
	b2BodyId flipper_right_id;

	b2WorldId world_id;
	SDL_Texture* atlas;

	map<b2BodyId, Entity*> entities;
	list<b2ShapeId> shape_ids;
	int entities_alive_count;

	int score;
	int lives;

};

// 0 = not charged, 1 = fully charged
static float launcher_charge_t(PinballGameState* state)
{
	return (state->power - power_min) / (power_max - power_min);
}

/*static float launcher_current_height(PinballGameState* state)
{
	return launcher_height - launcher_charge_t(state) * launcher_compression;
}*/


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

static b2BodyId create_static_box(PinballGameState* state, vec2f position, vec2f size, optional<float> rotation = 0)
{
	b2BodyDef  body_def  = b2DefaultBodyDef();
    b2ShapeDef shape_def = b2DefaultShapeDef();
    b2Polygon  polygon   = b2MakeBox(size.x, size.y);
	Entity*    entity    = new Entity();

	body_def.position = b2Vec2{position.x, position.y};
    body_def.rotation = b2MakeRot(-DEG_2_RAD * *rotation);

	shape_def.enableHitEvents     = true;
	shape_def.enableContactEvents = true;
	shape_def.filter.categoryBits = COLLISION_FILTER_OBSTACLE;
	shape_def.filter.maskBits     = COLLISION_FILTER_PLAYER;

    entity->body_id = b2CreateBody(state->world_id, &body_def);
    b2CreatePolygonShape(entity->body_id, &shape_def, &polygon);
	entity_create(state, entity, entity->body_id);
	
	return entity->body_id;
}

static b2BodyId create_kill_zone(PinballGameState* state, vec2f position, vec2f size, optional<float> rotation = 0)
{
	b2BodyDef  body_def  = b2DefaultBodyDef();
    b2ShapeDef shape_def = b2DefaultShapeDef();
    b2Polygon  polygon   = b2MakeBox(size.x, size.y);
	Entity*    entity    = new Entity();

	body_def.position = b2Vec2{position.x, position.y};
    body_def.rotation = b2MakeRot(-DEG_2_RAD * *rotation);
	shape_def.isSensor 			  = true;
	shape_def.enableSensorEvents  = true;

	shape_def.filter.categoryBits = COLLISION_KILLER;
	shape_def.filter.maskBits     = COLLISION_FILTER_PLAYER;

    entity->body_id = b2CreateBody(state->world_id, &body_def);
    b2CreatePolygonShape(entity->body_id, &shape_def, &polygon);
	entity_create(state, entity, entity->body_id);
	
	return entity->body_id;
}

static b2BodyId create_new_ball(PinballGameState* state, EngineContext * context, vec2f position, optional<float> rotation = 0)
{
	b2BodyDef  body_def  = b2DefaultBodyDef();
    b2ShapeDef shape_def = b2DefaultShapeDef();
    b2Circle circle		 = { { 0, 0 }, ball_radius };

	Entity*    entity    = new Entity();
	entity->transform.scale = vec2f{1, 1};
	entity->transform.rotation = *rotation;
	
	vec2f Ssize = itu_lib_sprite_get_world_size(context, &entity->sprite, &entity->transform);
	vec2f offset = -mul_element_wise(Ssize, entity->sprite.pivot - vec2f{ 0.5f, 0.5f });
	circle.center = value_cast(b2Vec2, offset);


	body_def.type      = b2_dynamicBody;
	body_def.position = b2Vec2{position.x, position.y};
	body_def.isEnabled = true;
    body_def.rotation = b2MakeRot(-DEG_2_RAD * *rotation);

	shape_def.density = 1;
	shape_def.enableHitEvents = true;
	shape_def.enableContactEvents = true;
	shape_def.enableSensorEvents = true;
	shape_def.filter.categoryBits = COLLISION_FILTER_PLAYER;
	shape_def.filter.maskBits     = COLLISION_FILTER_OBSTACLE | COLLISION_FILTER_SCORE_OBSTACLE | COLLISION_KILLER;

    entity->body_id = b2CreateBody(state->world_id, &body_def);
	b2CreateCircleShape(entity->body_id, &shape_def, &circle);
	entity_create(state, entity, entity->body_id);

	state->ball_body_id = &entity->body_id;

	itu_lib_sprite_init(
		&entity->sprite,
		state->atlas,
		itu_lib_sprite_get_source_rect(7, 7, 16, 16)
	);
	
	return entity->body_id;

}

static b2BodyId create_dynamic_box(PinballGameState* state, vec2f position, vec2f size, optional<float> rotation = 0)
{
	b2BodyDef  body_def  = b2DefaultBodyDef();
    b2ShapeDef shape_def = b2DefaultShapeDef();
    b2Polygon  polygon   = b2MakeBox(size.x, size.y);
	Entity*    entity    = new Entity();

	body_def.type     = b2_dynamicBody;
	body_def.position = b2Vec2{position.x, position.y};
    body_def.rotation = b2MakeRot(-DEG_2_RAD * *rotation);

	shape_def.density 			  = 1.0f;
    shape_def.material.friction   = 0.0f;
	shape_def.enableHitEvents     = true;
	shape_def.enableContactEvents = true;
	shape_def.filter.categoryBits = COLLISION_FILTER_OBSTACLE;

    entity->body_id = b2CreateBody(state->world_id, &body_def);
    b2CreatePolygonShape(entity->body_id, &shape_def, &polygon);
	entity_create(state, entity, entity->body_id);
	
	return entity->body_id;
}

static b2JointId create_joint_revolving(PinballGameState* state, b2BodyId a_body_id, vec2f a_point, optional<b2BodyId> b_body_id, vec2f b_point, optional<vec2f> limits = nullopt)
{
	b2RevoluteJointDef joint_def = b2DefaultRevoluteJointDef();

	if (!b_body_id)
	{
		b2BodyDef b_body_def = b2DefaultBodyDef();
		b_body_def.position = {b_point.x, b_point.y};
		b_body_def.rotation = b2Body_GetRotation(a_body_id);
		b_body_id = b2CreateBody(state->world_id, &b_body_def);
		b_point = VEC2F_ZERO;
	}

	if (limits)
	{
		joint_def.enableLimit = true;
		joint_def.lowerAngle = (-DEG_2_RAD * limits->x);
		joint_def.upperAngle = (-DEG_2_RAD * limits->y);
	}

    joint_def.bodyIdA = a_body_id;
    joint_def.bodyIdB = *b_body_id;
    joint_def.localAnchorA = {a_point.x, a_point.y};
    joint_def.localAnchorB = {b_point.x, b_point.y};

    return b2CreateRevoluteJoint(state->world_id, &joint_def);
}

static b2JointId create_joint_springing(PinballGameState* state, b2BodyId a_body_id, vec2f a_point, optional<b2BodyId> b_body_id, vec2f b_point)
{
	b2DistanceJointDef joint_def = b2DefaultDistanceJointDef();

	if (!b_body_id)
	{
		b2BodyDef b_body_def = b2DefaultBodyDef();
		b_body_def.position = {b_point.x, b_point.y};
		b_body_def.rotation = b2Body_GetRotation(a_body_id);
		b_body_id = b2CreateBody(state->world_id, &b_body_def);
		b_point = VEC2F_ZERO;
	}

	joint_def.enableSpring = true;
	joint_def.collideConnected = true;
	joint_def.hertz = 2.0f;
	joint_def.dampingRatio = 0.5f;

    joint_def.bodyIdA = a_body_id;
    joint_def.bodyIdB = *b_body_id;
    joint_def.localAnchorA = {a_point.x, a_point.y};
    joint_def.localAnchorB = {b_point.x, b_point.y};

    return b2CreateDistanceJoint(state->world_id, &joint_def);
}

static b2JointId create_joint_axislock(PinballGameState* state, b2BodyId a_body_id, char axis)
{
	b2PrismaticJointDef joint_def = b2DefaultPrismaticJointDef();

	b2BodyDef b_body_def = b2DefaultBodyDef();
	b_body_def.position = b2Body_GetPosition(a_body_id);
	auto b_body_id = b2CreateBody(state->world_id, &b_body_def);

	switch (axis)
	{
		case 'x':
			joint_def.localAxisA = { 1.0f, 0.0f };
			break;
		case 'y':
			joint_def.localAxisA = { 0.0f, 1.0f };
			break;
	}

    joint_def.bodyIdA = a_body_id;
    joint_def.bodyIdB = b_body_id;

    return b2CreatePrismaticJoint(state->world_id, &joint_def);
}



static void lose_life(PinballGameState* state, EngineContext * context, b2BodyId ballId)
{
	entity_destroy(state, ballId);
	state->lives--;

	if(state->lives <= 0){
		SDL_Log("YOU LOSE");
		return;
	}
	create_new_ball(state, context, {5.5f, 0.0f});
}


static void game_reset(EngineContext* context, PinballGameState* state)
{
	if(b2World_IsValid(state->world_id))
		b2DestroyWorld(state->world_id);

	b2WorldDef def_world = b2DefaultWorldDef();
	def_world.gravity.y = GRAVITY;
	state->world_id = b2CreateWorld(&def_world);
	b2World_SetHitEventThreshold(state->world_id, -5);

	state->phase = LAUNCHER_PHASE_READY;
	state->charging = false;
	state->power = power_min;
	state->request_reset = false;
	state->score = 0;
	state->lives = 3;

	// ball: dynamic body, disabled while it rests on the launcher

	create_new_ball(state, context, {5.5f, 0.0f});

	/*b2BodyDef body_def = b2DefaultBodyDef();
	body_def.type      = b2_dynamicBody;
	body_def.position = b2Vec2{5.5f, 0.0f};
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
	ball_entity->body_id = b2CreateBody(state->world_id, &body_def);
	state->ball_body_id = &ball_entity->body_id;

	
	shape_def.density = 1;
	shape_def.enableHitEvents = true;
	shape_def.enableContactEvents = true;
	shape_def.enableSensorEvents = true;
	shape_def.filter.categoryBits = COLLISION_FILTER_PLAYER;
	shape_def.filter.maskBits     = COLLISION_FILTER_OBSTACLE | COLLISION_FILTER_SCORE_OBSTACLE | COLLISION_KILLER;

	entity_create(state, ball_entity, ball_entity->body_id);

	b2CreateCircleShape(ball_entity->body_id, &shape_def, &circle);

	itu_lib_sprite_init(
		&ball_entity->sprite,
		state->atlas,
		itu_lib_sprite_get_source_rect(7, 7, 16, 16)
	);	
*/
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

	create_static_box(state, {0.5f, 8.0f}, {5.5f, 0.1f});
	create_static_box(state, {0.5f, -8.0f}, {5.5f, 0.1f});
	create_static_box(state, {-5.0f, 0.0f}, {0.1f, 8.0f});
	create_static_box(state, {5.0f, -1.0f}, {0.1f, 7.0f});
	create_static_box(state, {6.0f, 0.0f}, {0.1f, 8.0f});
	create_static_box(state, {5.5f, 7.5f}, {0.8f, 0.1f}, 45);
	create_static_box(state, {-4.0f, -3.0f}, {1.3f, 0.1f}, 40);
	create_static_box(state, {4.0f, -3.0f}, {1.3f, 0.1f}, -40);

	// Dead zone

	create_kill_zone(state, {0.0f, -7.0f}, {5.0f, 1.0f});

    // flippers

	state->flipper_left_id = create_dynamic_box(state, {-2.2f, -4.5f}, {1.1f, 0.1f}, 40);
	state->flipper_right_id = create_dynamic_box(state, {2.2f, -4.5f}, {1.1f, 0.1f}, 140);
	
    create_joint_revolving(state, state->flipper_left_id, {-1.1, 0}, nullopt, {-3, -3.8}, vec2f{80, 0});
	create_joint_revolving(state, state->flipper_right_id, {-1.1, 0}, nullopt, {3, -3.8}, vec2f{0, -80});

	// launcher

	state->launcher_body_id = create_dynamic_box(state, {5.5f, -6.0f}, {0.5f, 2.1f}, 0);

	create_joint_springing(state, state->launcher_body_id, {0.0f, 0.0f}, nullopt, {5.5f, -5.0f});
	create_joint_axislock(state, state->launcher_body_id, 'y');

	// debug draw

	debug_draw.context = context;
	debug_draw.drawShapes = true;
	debug_draw.DrawSolidPolygonFcn = fn_box2d_wrapper_draw_polygon;
	debug_draw.DrawSolidCircleFcn  = fn_box2d_wrapper_draw_circle;
}



static void game_update(EngineContext* context, PinballGameState* state)
{
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_RESET] || state->request_reset)
		game_reset(context, state);

	if(context->btn_isdown[BTN_TYPE_LEFT])
		b2Body_ApplyAngularImpulse(state->flipper_left_id, 5.0f, true);

	if(context->btn_isdown[BTN_TYPE_RIGHT])
		b2Body_ApplyAngularImpulse(state->flipper_right_id, -5.0f, true);

	if(context->btn_isdown[BTN_TYPE_ACTION_0])
	{
		b2Body_ApplyLinearImpulseToCenter(state->launcher_body_id, b2Vec2{0.0f, --state->power}, true);
		state->power = max(state->power, -60.0f);
	}
	else
		state->power = 0;
	
	// ↑ technically this is allowed but lowkey cursed xd
	

	{
		//b2ContactEvents contact_events = b2World_GetContactEvents(state->world_id);

		//b2ContactHitEvent * hitevents = contact_events.hitEvents;

		if(b2World_GetContactEvents(state->world_id).hitCount > 0)
		{
			//SDL_Log("HIT COUNTS: %i", b2World_GetContactEvents(state->world_id).hitCount);
			//b2ContactHitEvent * hitEvents = b2World_GetContactEvents(state->world_id).hitEvents;
			
			/*for(int i = 0; i < b2World_GetContactEvents(state->world_id).hitCount; ++i){
				b2ContactHitEvent event = hitEvents[i];

				b2Filter filter_a = b2Shape_GetFilter(event.shapeIdA);
				b2Filter filter_b = b2Shape_GetFilter(event.shapeIdB);
				if(filter_a.categoryBits & COLLISION_FILTER_SCORE_OBSTACLE){
					state->score += 10;
					
					SDL_Log("SCORE: %i", state->score);
					break;
				}
			}*/
		}
		if(b2World_GetContactEvents(state->world_id).beginCount > 0)
		{
			for(int i = 0; i < b2World_GetContactEvents(state->world_id).beginCount; ++i){
				b2ContactBeginTouchEvent event = b2World_GetContactEvents(state->world_id).beginEvents[i];

				b2Filter filter_a = b2Shape_GetFilter(event.shapeIdA);
				b2Filter filter_b = b2Shape_GetFilter(event.shapeIdB);
				if(filter_a.categoryBits & COLLISION_FILTER_SCORE_OBSTACLE){
					state->score += 10;
					
					SDL_Log("BEGIN TOUCH. SCORE: %i", state->score);
					//break;
				}
			}


		}

		if(b2World_GetSensorEvents(state->world_id).beginCount > 0)
		{
			for(int i = 0; i < b2World_GetSensorEvents(state->world_id).beginCount; ++i){
				b2SensorBeginTouchEvent event = b2World_GetSensorEvents(state->world_id).beginEvents[i];
				b2Filter filter_a = b2Shape_GetFilter(event.sensorShapeId);
				if(filter_a.categoryBits & COLLISION_KILLER){		
					SDL_Log("Ball dies");
					lose_life(state, context, b2Shape_GetBody(event.visitorShapeId));
					//break;
				}
			}
		}

		for(int i = 0; i < b2World_GetContactEvents(state->world_id).endCount; ++i){
				b2ContactEndTouchEvent event = b2World_GetContactEvents(state->world_id).endEvents[i];

				b2Filter filter_a = b2Shape_GetFilter(event.shapeIdA);
				b2Filter filter_b = b2Shape_GetFilter(event.shapeIdB);
				if(filter_a.categoryBits & COLLISION_FILTER_SCORE_OBSTACLE){					
					SDL_Log("END TOUCH.");
					//break;
				}
		}
		
	}
}

static void game_render(EngineContext* context, PinballGameState* state)
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

	{
		TTF_Font* Sans = TTF_OpenFont("Sans.ttf", 24);

		SDL_Color White = {255, 255, 255};

		string scoreString = "SCORE " + state->score;
		int value = scoreString.length();

		const char *cstr = scoreString.c_str();


		SDL_Surface* surfaceMessage =
			TTF_RenderText_Solid(Sans, cstr, 48, White); 

		SDL_Texture* Message = SDL_CreateTextureFromSurface(context->renderer, surfaceMessage);

		SDL_FRect Message_rect; 
		Message_rect.x = 0;  
		Message_rect.y = 0; 
		Message_rect.w = 100; 
		Message_rect.h = 100;

		SDL_RenderTexture(context->renderer, Message, NULL, &Message_rect);

		SDL_DestroySurface(surfaceMessage);
		SDL_DestroyTexture(Message);
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
			}
		}

		game_render(&context, &state);
		itu_lib_imgui_frame_end(&context);
		SDL_RenderPresent(context.renderer);
		itu_lib_context_frame_timing_update(&context);
	}
}