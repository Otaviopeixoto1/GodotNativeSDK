#include "game_object_base.h"

using namespace godot;
using namespace my_native;

GameObjectBase::GameObjectBase() {
}

GameObjectBase::~GameObjectBase() {
}

void GameObjectBase::set_health(int p_value) {
	health = p_value;
}

int GameObjectBase::get_health() const {
	return health;
}

void GameObjectBase::on_damage(int p_amount) {
	// Default implementation, project subclasses override this.
	health -= p_amount;
}

void GameObjectBase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_health", "value"), &GameObjectBase::set_health);
	ClassDB::bind_method(D_METHOD("get_health"), &GameObjectBase::get_health);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "health"), "set_health", "get_health");
}
