@echo off
call C:\Espressif\idf_cmd_init.bat
cd /d E:\agent\esp32_lua_llm
idf.py -p COM12 monitor
