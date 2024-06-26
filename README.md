# Admin system
My Discord server - https://discord.com/invite/g798xERK5Y
# TODO List
- [ ]  ...

## Require
- CS2 Server (Linux or Window)
- Remove server hibernation `sv_hibernate_when_empty 0`
- sql_mm plugins by zer0k-z : https://github.com/zer0k-z/sql_mm (In the release archive)
- Utils Api by Pisex : https://github.com/pisex/cs2-menus
- Specify database data in the config

## Commands
Admin functions can be used both in the console(mm_*) and in chat(!*)
The plugin includes the following commands:
- `!status` - Shows player userid.
- `!admin` - Display admin menu.
- `!ban <#userid|name> <duration(minutes)/0 (permanent)> <reason>` - Allows you to block a player(ADMFLAG_BAN)
- `!offban <steam64> <nick> <duration(minutes)/0 (permanent)> <reason>` - Offline ban(ADMFLAG_BAN)
- `!!rename <#userid|name> <name>` - Rename player(ADMFLAG_KICK)
- `!unban <steamid> - unban` - Allows you to unblock a player(ADMFLAG_UNBAN)
- `!silence <#userid|name> <duration(minutes)/0 (permanent)> <reason>` - Allows you to block voice and chat of a player (ADMFLAG_CHAT)
- `!unsilence <#userid|name>` - Allows you to unlock the voice and chat of the player (ADMFLAG_CHAT)
- `!mute <#userid|name> <duration(minutes)/0 (permanent)> <reason>` - Allows you to block a player's voice (ADMFLAG_CHAT)
- `!unmute <#userid|name>` - Allows you to unlock player voice (ADMFLAG_CHAT)
- `!gag <#userid|name> <duration(minutes)/0 (permanent)> <reason>` - Allows you to block a player's chat (ADMFLAG_CHAT)
- `!ungag <#userid|name>` - Allows you to unlock player chat (ADMFLAG_CHAT)
- `!csay <message>>` - say to all players (in center) (ADMFLAG_CHAT)
- `!hsay <message>` - say to all players (in hud) (ADMFLAG_CHAT)
- `!kick <#userid|name>` - Allows you to kick a player (ADMFLAG_KICK)
- `!who <optional #userid|name>` - Allows you to recognize the player (ADMFLAG_GENERIC)
- `!rcon <command>` - Allows you to send a command on behalf of the console (ADMFLAG_RCON)
- `!slay <#userid|name>` - Allows you to kill a player (ADMFLAG_SLAY)
- `!slap <#userid|name> <optional damage>` - Allows you to slap a player (ADMFLAG_SLAY)
- `!setteam <#userid|name> <team (0-3)>` - Allows you to change a player's command(without death) (ADMFLAG_SLAY)
- `!changeteam <#userid|name> <team (0-3)>` - Allows you to change a player's command(with death) (ADMFLAG_SLAY)
- `!map <mapname>` - Allows you to change the map (ADMFLAG_CHANGEMAP)
- `!noclip <optional #userid|name>` - Allows you to enable noclip (ADMFLAG_CHEATS)
- `!add_admin <admin_name> <steamid64> <duration(minutes)/0 (permanent)> <flags> <immunity> <optional comment>` - Allows you to add an administrator (ADMFLAG_ROOT)
- `!remove_admin <steamid64>` - Allows you to remove an administrator (ADMFLAG_ROOT)

## Configuration
- Databases file: `addons/configs/databases.cfg`
- Maps file: `addons/configs/maplist.ini`
- Times and Reason file: `addons/configs/admin.ini`
- Translation file: `addons/translations/admin_system.phrases.txt`
