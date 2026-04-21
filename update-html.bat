@echo off
echo Uploading index.html to ESP32...
curl -X POST http://192.168.1.44/update-html -F "file=@data\index.html" -H "Content-Type: multipart/form-data"
echo.
echo Done! Refresh phone browser to see changes.