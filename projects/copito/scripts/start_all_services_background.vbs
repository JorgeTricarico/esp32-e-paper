Set WshShell = CreateObject("WScript.Shell")
WshShell.CurrentDirectory = "C:\Users\admin\Documents\Github\esp32-paper\projects\copito"
WshShell.Run """C:\Users\admin\AppData\Local\hermes\hermes-agent\venv\Scripts\pythonw.exe"" ""C:\Users\admin\Documents\Github\esp32-paper\projects\copito\scripts\copito_services_launcher.py""", 0, False
