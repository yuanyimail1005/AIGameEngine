-- Sweeper: a kinematic bar rotating around the platform center.
local Sweeper = {}

function Sweeper:OnCreate()
	self.speed = 0.9
end

function Sweeper:OnUpdate(dt)
	self.entity:Rotate(vec3(0, self.speed * dt, 0))
end

return Sweeper
