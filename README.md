# Admin system

## Require
- CS2 Serveur (Linux or Window)
- Remove server hibernation `sv_hibernate_when_empty 0`
- mysql_mm plugins by Poggu : https://github.com/Poggicek/mysql_mm (In the release archive)
- Specify database data in the config

## Commands
Admin functions can be used both in the console(mm_*) and in chat(!*)
The plugin includes the following commands:
- `!admin` - Display available commands.
- `!ban <#userid|name> <duration(seconds)/0 (permanent)> <reason>` - Allows you to block a player(ADMFLAG_BAN)
- `!unban <steamid> - unban` - Allows you to unblock a player(ADMFLAG_UNBAN)
- `!silence <#userid|name> <duration(seconds)/0 (permanent)> <reason>` - Allows you to block voice and chat of a player (ADMFLAG_CHAT)
- `!unsilence <#userid|name>` - Allows you to unlock the voice and chat of the player (ADMFLAG_CHAT)
- `!mute <#userid|name> <duration(seconds)/0 (permanent)> <reason>` - Allows you to block a player's voice (ADMFLAG_CHAT)
- `!unmute <#userid|name>` - Allows you to unlock player voice (ADMFLAG_CHAT)
- `!gag <#userid|name> <duration(seconds)/0 (permanent)> <reason>` - Allows you to block a player's chat (ADMFLAG_CHAT)
- `!ungag <#userid|name>` - Allows you to unlock player chat (ADMFLAG_CHAT)
- `!csay <message>>` - say to all players (in center) (ADMFLAG_CHAT)
- `!hsay <message>` - say to all players (in hud) (ADMFLAG_CHAT)
- `!kick <#userid|name>` - Allows you to kick a player (ADMFLAG_KICK)
- `!who <optional #userid|name>` - Allows you to recognize the player (ADMFLAG_GENERIC)
- `!rcon <command>` - Allows you to send a command on behalf of the console (ADMFLAG_RCON)
- `!freeze <#userid|name> <duration>` - Allows you to freeze a player (ADMFLAG_SLAY)
- `!unfreeze <#userid|name>` - Allows you to unfreeze a player (ADMFLAG_SLAY)
- `!slay <#userid|name>` - Allows you to kill a player (ADMFLAG_SLAY)
- `!slap <#userid|name> <optional damage>` - Allows you to slap a player (ADMFLAG_SLAY)
- `!changeteam <#userid|name> <team (0-3)>` - Allows you to change a player's command (ADMFLAG_SLAY)
- `!map <mapname>` - Allows you to change the map (ADMFLAG_CHANGEMAP)
- `!noclip <optional #userid|name>` - Allows you to enable noclip (ADMFLAG_CHEATS)
- `!reload_admins` - Allows you to reload the list of administrators (ADMFLAG_ROOT)

## Configuration
- Admins file: `addons/configs/admins.cfg`
- Databases file: `addons/configs/databases.cfg`