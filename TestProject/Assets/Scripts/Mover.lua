-- Moves a kinematic body back and forth; physics follows the transform.
local Mover = {}

function Mover:OnCreate()
	self.origin = self.entity:GetPosition()
end

function Mover:OnUpdate(dt)
	local t = Scene.GetTime()
	self.entity:SetPosition(self.origin + vec3(math.sin(t * 2.0) * 2.0, 0, 0))
end

return Mover
