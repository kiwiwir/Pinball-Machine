#include <iostream>
#include <gameObjectsLib.hpp>


Tilemap::Tilemap(GameObject &gameObject, SDL_Texture * texture, map<vec2f, SDL_FRect> textureMap) : MonoBehavior(gameObject), Rendered()
{
    this->name = "Tilemap";

    for(auto pair : textureMap){
        Sprite * sprite = new Sprite();
        
        
        itu_lib_sprite_init(
            sprite,
            texture,
            itu_lib_sprite_get_source_rect(pair.second.x, pair.second.y, pair.second.w, pair.second.h)
        );

        this->tiles.insert({pair.first, sprite});
    }
    
}

bool operator<(const vec2f& lhs, const vec2f& rhs)
{
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    } else {
        return lhs.y < rhs.y;
    }
}
bool operator==(const vec2f& lhs, const vec2f& rhs)
{
	if(lhs.x == rhs.x && lhs.y == rhs.y){
   		 return true;
	}
	else{
		return false;
	}
};


void Tilemap::Print() 
{
    std::cout << "This is a Sprite2D class";
}

void Tilemap::Update(float delta) {

}

void Tilemap::Render(EngineContext * context){

    for (auto pair : this->tiles)
    {
        Transform2D transformTile = gameObject.transform;
        transformTile.position = transformTile.position + pair.first;

        itu_lib_sprite_render(context, pair.second, &transformTile);
    }
}

void Tilemap::Start() {

}

void Tilemap::Awake() {
    MonoBehavior::Awake();
}

void Tilemap::Destroy(){
    delete this;
}

