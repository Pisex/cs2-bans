# cs2-bans
Installation:
1. Install the latest version of [metamod](https://www.metamodsource.net/downloads.php/?branch=master) in folder csgo
2. Add the ```Game csgo/addons/metamod``` line after the ```Game_LowViolence csgo_lv``` line to the csgo/gameinfo.gi file
3. Drop the release files into the csgo folder
4. Add to the `csgo/addons/configs/admin_base/admins.ini` file your steamid in the format `STEAM_1:1:568722805`
5. Commands

```mm_reload_admins``` - Reload admin list

```mm_ban <userid|nickname> <time_second>``` - Ban client

```mm_unban <steamid>``` - Unban client

```mm_kick <userid|nickname>``` - Kick client

```mm_map <mapname>``` - Change map

gameinfo.gi IS RESET AFTER EVERY UPDATE
