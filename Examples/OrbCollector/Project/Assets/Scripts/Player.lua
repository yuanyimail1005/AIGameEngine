-- Player: rolls the ball with forces relative to the camera; respawns after falling off.
local Player = {}

function Player:OnCreate()
	self.force = 18
	self.spawn = self.entity:GetPosition()
end

function Player:OnUpdate(dt)
	local input = vec3(0, 0, 0)
	if Input.IsKeyDown("W") or Input.IsKeyDown("Up") then input.z = input.z - 1 end
	if Input.IsKeyDown("S") or Input.IsKeyDown("Down") then input.z = input.z + 1 end
	if Input.IsKeyDown("A") or Input.IsKeyDown("Left") then input.x = input.x - 1 end
	if Input.IsKeyDown("D") or Input.IsKeyDown("Right") then input.x = input.x + 1 end
	if input:length() > 0 then
		self.entity:AddForce(input:normalized() * self.force)
	end
	if self.entity:GetPosition().y < -10 then
		self:Respawn()
	end
end

function Player:Respawn()
	self.entity:SetPosition(self.spawn)
	self.entity:SetVelocity(vec3(0, 0, 0))
	self.entity:SetAngularVelocity(vec3(0, 0, 0))
	Scene.FindEntityByName("GameManager"):GetScript():OnPlayerFell()
end

return Player
