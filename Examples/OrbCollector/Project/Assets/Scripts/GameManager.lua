-- GameManager: spawns orbs, tracks score and time, decides win / loss.
local Game = {}

function Game:OnCreate()
	self.total = 8
	self.collected = 0
	self.falls = 0
	self.timeLimit = 90
	self.state = "playing"
	Math.Seed(7)
	for i = 1, self.total do
		local angle = (i / self.total) * math.pi * 2
		local radius = 3 + (i % 2) * 3
		Scene.Instantiate("Prefabs/Orb.eprefab", vec3(math.cos(angle) * radius, 0.8, math.sin(angle) * radius))
	end
	Log.Info("Collect " .. self.total .. " orbs in " .. self.timeLimit .. " seconds!")
end

-- HUD: score and countdown in the screen-space UI, plus a centered message when the game ends.
function Game:UpdateHUD()
	local score = Scene.FindEntityByName("ScoreText")
	if score then
		score:SetComponent("UIText", { Text = "Orbs " .. self.collected .. " / " .. self.total })
	end
	local timer = Scene.FindEntityByName("TimerText")
	if timer then
		local remaining = math.max(0, self.timeLimit - Scene.GetTime())
		local color = remaining < 10 and { 1, 0.35, 0.3, 1 } or { 1, 1, 1, 1 }
		timer:SetComponent("UIText", { Text = string.format("Time %d", math.ceil(remaining)), Color = color })
	end
end

function Game:ShowMessage(text, color)
	local message = Scene.FindEntityByName("MessageText")
	if message then
		message:SetComponent("UIText", { Text = text, Color = color, Visible = true })
	end
end

function Game:OnUpdate(dt)
	self:UpdateHUD()
	if self.state ~= "playing" then return end
	if Scene.GetTime() > self.timeLimit then
		self.state = "lost"
		self:ShowMessage("Time's up!", { 1, 0.4, 0.3, 1 })
		Log.Warn("Time is up! Orbs collected: " .. self.collected .. "/" .. self.total)
	end
end

function Game:OnOrbCollected()
	self.collected = self.collected + 1
	-- Each collected orb lights up one marker on the scoreboard.
	local marker = Scene.FindEntityByName("Marker" .. self.collected)
	if marker then
		marker:SetComponent("MeshRenderer", { MaterialOverride = { EmissiveColor = vec3(0.2, 1.0, 0.4), EmissiveStrength = 3 } })
	end
	Log.Info("Orb " .. self.collected .. "/" .. self.total)
	if self.collected >= self.total then
		self.state = "won"
		self:ShowMessage(string.format("You win!\n%.1f seconds", Scene.GetTime()), { 0.4, 1, 0.5, 1 })
		Audio.PlayOneShot("Audio/win.wav", 1.0)
		Log.Info("You win! Time: " .. string.format("%.1f", Scene.GetTime()) .. "s, falls: " .. self.falls)
	end
end

function Game:OnPlayerFell()
	self.falls = self.falls + 1
	Audio.PlayOneShot("Audio/fall.wav", 0.8)
end

return Game
