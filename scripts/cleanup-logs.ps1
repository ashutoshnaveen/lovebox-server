Set-Location 'D:\lovebox-server-with-secrets'
$git = 'C:\Program Files\Git\cmd\git.exe'
& $git rm --cached monitor_log.txt upload_log.txt
Add-Content .gitignore "`nupload_log.txt`nmonitor_log.txt"
& $git add .gitignore
& $git commit -m 'Remove stray logs from repo'
& $git push origin main
& $git log -1 --oneline
