// server/server.js

const express = require('express');
const multer  = require('multer');
const path    = require('path');
const fs      = require('fs');

const app = express();
const PORT = 3000;

// Nơi tạm chứa upload trước khi rename
const upload = multer({ dest: 'uploads/' });

// Trang upload firmware
app.get('/', (req, res) => {
  res.send(`
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="utf-8">
      <title>Upload Firmware for OTA</title>
      <style>
        body { font-family: Arial; text-align: center; margin-top: 50px; }
        input { margin: 10px; }
      </style>
    </head>
    <body>
      <h2>Upload Firmware for OTA</h2>
      <form action="/upload" method="post" enctype="multipart/form-data">
        <input type="file" name="firmware" accept=".bin" required /><br/>
        <button type="submit">Upload & Save</button>
      </form>
    </body>
    </html>
  `);
});

// Xử lý POST upload và lưu thành firmware-v0.bin
app.post('/upload', upload.single('firmware'), (req, res) => {
  if (!req.file) {
    return res.status(400).send('No file uploaded.');
  }
  const tempPath = req.file.path;
  const targetPath = path.join(__dirname, 'firmware-v0.bin');

  // Xóa file cũ nếu có
  if (fs.existsSync(targetPath)) {
    fs.unlinkSync(targetPath);
  }

  fs.rename(tempPath, targetPath, err => {
    if (err) {
      console.error('Error saving firmware:', err);
      return res.status(500).send('Failed to save firmware.');
    }
    console.log('Firmware saved to', targetPath);
    res.send(`
      <p>Upload successful! Saved as <strong>firmware-v0.bin</strong></p>
      <p><a href="/">Back to upload page</a></p>
    `);
  });
});

// Phục vụ firmware để ESP32 có thể GET /firmware-v0.bin
app.get('/firmware-v0.bin', (req, res) => {
  const filePath = path.join(__dirname, 'firmware-v0.bin');
  if (fs.existsSync(filePath)) {
    res.setHeader('Content-Type', 'application/octet-stream');
    res.sendFile(filePath);
  } else {
    res.status(404).send('Firmware not found.');
  }
});

app.listen(PORT, () => {
  console.log(`OTA server listening on http://0.0.0.0:${PORT}`);
});
