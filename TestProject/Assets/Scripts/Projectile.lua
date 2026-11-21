-- Flies along its forward axis and destroys itself after a short lifetime.
local Projectile = {}

function Projectile:OnCreate()
	self.age = 0
	self.lifetime = 0.25
	ProjectileStats = ProjectileStats or { created = 0, destroyed = 0 }
	ProjectileStats.created = ProjectileStats.created + 1
end

function Projectile:OnUpdate(dt)
	self.age = self.age + dt
	self.entity:Translate(self.entity:GetForward() * (8 * dt))
	if self.age >= self.lifetime then
		self.entity:Destroy()
	end
end

function Projectile:OnDestroy()
	ProjectileStats.destroyed = ProjectileStats.destroyed + 1
end

function Projectile:GetAge()
	return self.age
end

return Projectile
