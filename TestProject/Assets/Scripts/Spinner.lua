-- Spins around Y; counts update and late-update calls.
local Spinner = {}

function Spinner:OnCreate()
	self.updates = 0
	self.lateUpdates = 0
end

function Spinner:OnUpdate(dt)
	self.updates = self.updates + 1
	self.entity:Rotate(vec3(0, 1.5 * dt, 0))
end

function Spinner:OnLateUpdate(dt)
	self.lateUpdates = self.lateUpdates + 1
end

return Spinner
