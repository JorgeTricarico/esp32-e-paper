Set WshShell = CreateObject("WScript.Shell")
WshShell.CurrentDirectory = "C:\Users\admin\Documents\Github\esp32-paper\projects\copito\simulator"
WshShell.Run "python -m http.server 8080 --bind 0.0.0.0", 0, False
