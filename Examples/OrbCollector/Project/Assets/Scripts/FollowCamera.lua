-- Camera: follows the player smoothly from above and behind.
local Follow = {}

function Follow:OnCreate()
	self.offset = vec3(0, 9, 11)
	self.target = Scene.FindEntityByName("Player")
end

function Follow:OnLateUpdate(dt)
	if not self.target:IsValid() then return end
	local goal = self.target:GetWorldPosition() + self.offset
	local position = self.entity:GetPosition()
	self.entity:SetPosition(position:lerp(goal, Math.Clamp(dt * 4, 0, 1)))
	self.entity:LookAt(self.target:GetWorldPosition())
end

return Follow
