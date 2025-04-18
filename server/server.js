const express      = require('express');
const fileUpload   = require('express-fileupload');
const path         = require('path');
const fs           = require('fs');

const app = express();
const PORT = 3000;

// Kích hoạt middleware để parse multipart/form-data
app.use(fileUpload({
  limits: { fileSize: 50 * 1024 * 1024 }, // 50MB giới hạn
}));

// Trang form upload
app.get('/', (req, res) => {
  res.send(`
    <!DOCTYPE html>
    <html>
      <head>
        <meta charset="utf-8">
        <title>Firmware OTA Upload</title>
        <style>
          body { font-family: Arial, sans-serif; padding: 2rem; text-align: center; }
          form { margin-top: 2rem; }
          input[type=file] { padding: .5rem; }
          input[type=submit] { margin-top: 1rem; padding: .5rem 1rem; }
        </style>
      </head>
      <body>
        <h1>Upload Firmware for OTA</h1>
        <form method="POST" action="/upload" enctype="multipart/form-data">
          <input type="file" name="firmware" accept=".bin" required>
          <br>
          <input type="submit" value="Upload & Save">
        </form>
      </body>
    </html>
  `);
});

// Xử lý upload
app.post('/upload', (req, res) => {
  if (!req.files || !req.files.firmware) {
    return res.status(400).send('No firmware file uploaded.');
  }

  const firmware = req.files.firmware;
  const savePath = path.join(__dirname, 'firmware.bin');

  firmware.mv(savePath, err => {
    if (err) {
      console.error('Upload error:', err);
      return res.status(500).send('Failed to save firmware.');
    }
    console.log(`Firmware saved to ${savePath} (${firmware.size} bytes)`);
    res.send(`
      <p>Upload successful! Saved as <code>${savePath}</code></p>
      <p><a href="/">Back to upload page</a></p>
    `);
  });
});

// Khởi động server
app.listen(PORT, () => {
  console.log(`OTA upload server running: http://localhost:${PORT}/`);
});
