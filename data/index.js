const https = require('https');

const accessToken = 'MM197SnHT4VLZ8L98fPw';  // 🔁 Thay bằng access token thật
const host = 'app.coreiot.io';

// Thời điểm hiện tại và trước đó 10 phút
const now = Date.now();
const tenMinutesAgo = now - 10 * 60 * 1000;

// Gửi 2 giá trị tăng dần
const payload = [  // Giá trị cũ
  { ts: now, values: { meterReadingDelta: 100 } }             // Giá trị mới
];

const data = JSON.stringify(payload);

const options = {
  hostname: host,
  port: 443,
  path: `/api/v1/${accessToken}/telemetry`,
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
    'Content-Length': Buffer.byteLength(data)
  }
};

const req = https.request(options, res => {
  console.log(`✅ Status: ${res.statusCode}`);
  res.on('data', d => process.stdout.write(d));
});

req.on('error', error => {
  console.error('❌ Error:', error);
});

req.write(data);
req.end();
