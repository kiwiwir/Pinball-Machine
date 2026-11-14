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
    worldDef.gravity = (b2Vec2){0.0f, -15.0f};
    b2WorldId worldId = b2CreateWorld(&worldDef);
    b2DebugDraw debug_draw;

    // walls

    b2BodyDef  wallBodyDef  = b2DefaultBodyDef();
    b2ShapeDef wallShapeDef = b2DefaultShapeDef();
    b2Polygon  wallHorizontal = b2MakeBox(5.0f, 0.1f);
    b2Polygon  wallVertical   = b2MakeBox(0.1f, 8.0f);
    b2Polygon  wallInside     = b2MakeBox(1.5f, 0.1f);

    wallBodyDef.position = (b2Vec2){0.0f, 8.0f};
    b2BodyId wallTopId = b2CreateBody(worldId, &wallBodyDef);
    b2CreatePolygonShape(wallTopId, &wallShapeDef, &wallHorizontal);

    wallBodyDef.position = (b2Vec2){0.0f, -8.0f};
    b2BodyId wallBottomId = b2CreateBody(worldId, &wallBodyDef);
    b2CreatePolygonShape(wallBottomId, &wallShapeDef, &wallHorizontal);

    wallBodyDef.position = (b2Vec2){-5.0f, 0.0f};
    b2BodyId wallLeftId = b2CreateBody(worldId, &wallBodyDef);
    b2CreatePolygonShape(wallLeftId, &wallShapeDef, &wallVertical);

    wallBodyDef.position = (b2Vec2){5.0f, 0.0f};
    b2BodyId wallRightId = b2CreateBody(worldId, &wallBodyDef);
    b2CreatePolygonShape(wallRightId, &wallShapeDef, &wallVertical);

    wallBodyDef.position = (b2Vec2){-4.0f, -3.0f};
    wallBodyDef.rotation = b2MakeRot(-0.7f);
    b2BodyId wallInsideLeftId = b2CreateBody(worldId, &wallBodyDef);
    b2CreatePolygonShape(wallInsideLeftId, &wallShapeDef, &wallInside);

    wallBodyDef.position = (b2Vec2){4.0f, -3.0f};
    wallBodyDef.rotation = b2MakeRot(0.7f);
    b2BodyId wallInsideRightId = b2CreateBody(worldId, &wallBodyDef);
    b2CreatePolygonShape(wallInsideRightId, &wallShapeDef, &wallInside);

    // ball

    b2BodyDef  ballBodyDef  = b2DefaultBodyDef();
    b2ShapeDef ballShapeDef = b2DefaultShapeDef();
    b2Circle   ball;

    ballBodyDef.type = b2_dynamicBody;
    ballShapeDef.density = 1.0f;
    ballShapeDef.material.friction = 0.001f;
    ball.radius = 0.5f;

    ballBodyDef.position = (b2Vec2){-3.0f, 4.0f};
    b2BodyId ballId = b2CreateBody(worldId, &ballBodyDef);
    b2CreateCircleShape(ballId, &ballShapeDef, &ball);

    // flippers

    b2BodyDef  flipperBodyDef  = b2DefaultBodyDef();
    b2ShapeDef flipperShapeDef = b2DefaultShapeDef();
    b2Polygon  flipper = b2MakeBox(1.0f, 0.1f);

    flipperBodyDef.type = b2_dynamicBody;
    flipperShapeDef.density = 1.0f;
    flipperShapeDef.material.friction = 0.0f;

    flipperBodyDef.position = (b2Vec2){-2.2f, -4.5f};
    flipperBodyDef.rotation = b2MakeRot(-0.7f);
    b2BodyId flipperLeftId = b2CreateBody(worldId, &flipperBodyDef);
    b2CreatePolygonShape(flipperLeftId, &flipperShapeDef, &flipper);

    flipperBodyDef.position = (b2Vec2){2.2f, -4.5f};
    flipperBodyDef.rotation = b2MakeRot(0.7f);
    b2BodyId flipperRightId = b2CreateBody(worldId, &flipperBodyDef);
    b2CreatePolygonShape(flipperRightId, &flipperShapeDef, &flipper);

    // joints

    b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
    jointDef.enableLimit = true;

    jointDef.bodyIdA = wallInsideLeftId;
    jointDef.bodyIdB = flipperLeftId;
    jointDef.localAnchorA = {1.5f, 0.0f};
    jointDef.localAnchorB = {-1.0f, 0.0f};
    jointDef.lowerAngle = 0.0f;
    jointDef.upperAngle = 1.5f;
    b2JointId jointLeftId = b2CreateRevoluteJoint(worldId, &jointDef);

    jointDef.bodyIdA = wallInsideRightId;
    jointDef.bodyIdB = flipperRightId;
    jointDef.localAnchorA = {-1.5f, 0.0f};
    jointDef.localAnchorB = {1.0f, 0.0f};
    jointDef.lowerAngle = -1.5f;
    jointDef.upperAngle = 0.0f;
    b2JointId jointRightId = b2CreateRevoluteJoint(worldId, &jointDef);

    // debug draw

    debug_draw.context = &context;
	debug_draw.drawShapes = true;
	debug_draw.DrawSolidPolygonFcn = fn_box2d_wrapper_draw_polygon;
	debug_draw.DrawSolidCircleFcn = fn_box2d_wrapper_draw_circle;

    bool quit = false;
    bool buttonz = false;
    bool buttonx = false;

    SDL_Event event;
    while(!quit)
    {
        // stop game when `x` button is pressed on the window border UI
        while(SDL_PollEvent(&event))
        {
            switch(event.type)
                {
                    case SDL_EVENT_QUIT:
                        quit = true;
                        break;

                    case SDL_EVENT_KEY_UP:
                    case SDL_EVENT_KEY_DOWN:
                        if(event.key.key == SDLK_Z)
                            buttonz = event.key.down;
                        if(event.key.key == SDLK_X)
                            buttonx = event.key.down;
                        break;
                }
        }

        if (buttonz)
            b2Body_ApplyAngularImpulse(flipperLeftId, 5.0f, true);
        if (buttonx)
            b2Body_ApplyAngularImpulse(flipperRightId, -5.0f, true);

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
