#pragma once

#include "api.h"
#include <godot_cpp/classes/node2d.hpp>

namespace GDNativeSDK {

// Base class for gameplay objects. This class lives in the static library
// and is fixed. Project specific classes (Player, Enemy, ...) extend it
// from res://native and are compiled separately, per project.
class MY_NATIVE_API GameObjectBase : public godot::Node2D {
	GDCLASS(GameObjectBase, godot::Node2D)

public:
	GameObjectBase();
	~GameObjectBase() override;

	void set_health(int p_value);
	int get_health() const;

	// Hook for project subclasses. Default does nothing.
	virtual void on_damage(int p_amount);

protected:
	static void _bind_methods();

private:
	int health = 100;
};

}
