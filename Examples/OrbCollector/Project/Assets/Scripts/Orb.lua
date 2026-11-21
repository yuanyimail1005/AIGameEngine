-- Orb: bobs and spins; collected when the player touches it.
local Orb = {}

function Orb:OnCreate()
	self.base = self.entity:GetPosition()
	self.phase = Math.Random(0, 6.28)
end

function Orb:OnUpdate(dt)
	self.phase = self.phase + dt * 3
	self.entity:SetPosition(self.base + vec3(0, math.sin(self.phase) * 0.15, 0))
	self.entity:Rotate(vec3(0, dt * 2, 0))
end

function Orb:OnTriggerBegin(other)
	if other.name == "Player" and not self.collected then
		self.collected = true
		Audio.PlayOneShot("Audio/pickup.wav", 0.8, self.entity:GetWorldPosition())
		Scene.FindEntityByName("GameManager"):GetScript():OnOrbCollected()
		self.entity:Destroy()
	end
end

return Orb
