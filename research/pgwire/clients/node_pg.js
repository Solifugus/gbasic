const { Client } = require('pg');
(async () => {
  const c = new Client({ host: '127.0.0.1', port: +process.argv[2], database: 'pgcap' });
  await c.connect();
  await c.query('select id, name, since from customer where id = $1', [1]);
  await c.query('select total from invoice');
  await c.end();
})().catch(e => { console.error(e); process.exit(1); });
