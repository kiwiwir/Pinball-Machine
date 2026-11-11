#include <itu_engine.hpp>

int main(void)
{
    EngineContext context = { 0 };
    EngineConfig config;
	config.application_name = "Pinball";
	config.texture_pixels_per_unit = 128;
	config.step_per_second_fluid = 60;

    itu_lib_context_init(&config, &context);
    itu_lib_context_frame_timing_setup(&context);

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, -7.0f};
    b2WorldId worldId = b2CreateWorld(&worldDef);
    b2DebugDraw debug_draw;

    // walls
    {
        b2BodyDef  wallBodyDef  = b2DefaultBodyDef();
        b2ShapeDef wallShapeDef = b2DefaultShapeDef();
        b2Polygon  wallHorizontalBox = b2MakeBox(5.0f, 0.1f);
        b2Polygon  wallVerticalBox   = b2MakeBox(0.1f, 8.0f);

        wallBodyDef.position = (b2Vec2){0.0f, 8.0f};
        b2BodyId wallTopId = b2CreateBody(worldId, &wallBodyDef);

        wallBodyDef.position = (b2Vec2){0.0f, -8.0f};
        b2BodyId wallBottomId = b2CreateBody(worldId, &wallBodyDef);

        wallBodyDef.position = (b2Vec2){-5.0f, 0.0f};
        b2BodyId wallLeftId = b2CreateBody(worldId, &wallBodyDef);

        wallBodyDef.position = (b2Vec2){5.0f, 0.0f};
        b2BodyId wallRightId = b2CreateBody(worldId, &wallBodyDef);

        b2CreatePolygonShape(wallTopId,    &wallShapeDef, &wallHorizontalBox);
        b2CreatePolygonShape(wallBottomId, &wallShapeDef, &wallHorizontalBox);
        b2CreatePolygonShape(wallLeftId,   &wallShapeDef, &wallVerticalBox);
        b2CreatePolygonShape(wallRightId,  &wallShapeDef, &wallVerticalBox);

        // temp wall
        b2Polygon wallTempBox = b2MakeBox(2.5f, 0.1f);
        wallBodyDef.position = (b2Vec2){-3.0f, -3.0f};
        wallBodyDef.rotation = b2MakeRot(-0.7f);
        b2BodyId wallTempId = b2CreateBody(worldId, &wallBodyDef);
        b2CreatePolygonShape(wallTempId, &wallShapeDef, &wallTempBox);
        wallBodyDef.position = (b2Vec2){3.0f, -3.0f};
        wallBodyDef.rotation = b2MakeRot(0.7f);
        b2BodyId wallTemp2Id = b2CreateBody(worldId, &wallBodyDef);
        b2CreatePolygonShape(wallTemp2Id, &wallShapeDef, &wallTempBox);

    }

    // ball
    {
        b2BodyDef bodyDef   = b2DefaultBodyDef();
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        b2Circle ball;

        bodyDef.type = b2_dynamicBody;
        bodyDef.position = (b2Vec2){-3.0f, 4.0f};
        shapeDef.density = 1.0f;
        shapeDef.material.friction = 0.3f;
        ball.radius = 0.5f;

        b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
        b2CreateCircleShape(bodyId, &shapeDef, &ball);
    }

    debug_draw.context = &context;
	debug_draw.drawShapes = true;
	debug_draw.DrawSolidPolygonFcn = fn_box2d_wrapper_draw_polygon;
	debug_draw.DrawSolidCircleFcn = fn_box2d_wrapper_draw_circle;

    bool quit = false;

    SDL_Event event;
    while(!quit)
    {
        // stop game when `x` button is pressed on the window border UI
        while(SDL_PollEvent(&event))
            if(event.type == SDL_EVENT_QUIT)
                quit = true;

        // physics
        {
			context.physics_steps_per_frame_count = 0;
			while(context.accumulator_physics >= context.target_framerate_fixed_ns && context.physics_steps_per_frame_count < config.physics_steps_per_frame_max)
			{
				b2World_Step(worldId, NS_TO_SECONDS(context.target_framerate_fixed_ns), 4);
				++context.physics_steps_per_frame_count;
				context.accumulator_physics -= context.target_framerate_fixed_ns;
			}
		}

        // clear the drawing surface to a dark blue
        SDL_SetRenderDrawColor(context.renderer, 0x0C, 0x42, 0xA1, 0x00);
        SDL_RenderClear(context.renderer);

        // show frame
        b2World_Draw(worldId, &debug_draw);
        SDL_RenderPresent(context.renderer);

        // frame time
        itu_lib_context_frame_timing_update(&context);
    }
}
