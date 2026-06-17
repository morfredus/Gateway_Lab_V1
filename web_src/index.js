function fmtUptime(ms) {
  var s = Math.floor(ms / 1000);
  var m = Math.floor(s / 60); s %= 60;
  var h = Math.floor(m / 60); m %= 60;
  var d = Math.floor(h / 24); h %= 24;
  return (d > 0 ? d + 'j ' : '') + h + 'h ' + m + 'm ' + s + 's';
}
function rssiQuality(r) {
  if (r >= -50) return 'Excellent';
  if (r >= -60) return 'Bon';
  if (r >= -70) return 'Moyen';
  return 'Faible';
}
function refresh() {
  fetch('/api/status')
    .then(function(r) { return r.json(); })
    .then(function(d) {
      document.getElementById('ssid').textContent     = d.ssid || '—';
      document.getElementById('ip').textContent       = d.ip   || '—';
      document.getElementById('rssi').textContent     = d.rssi + ' dBm (' + rssiQuality(d.rssi) + ')';
      document.getElementById('hostname').textContent = d.hostname ? d.hostname + '.local' : '—';
      document.getElementById('uptime').textContent   = fmtUptime(d.uptime || 0);
      if (d.version) {
        document.getElementById('site-ver').textContent   = 'v' + d.version;
        document.getElementById('footer-mdns').textContent = d.hostname ? d.hostname + '.local' : '';
      }
      document.getElementById('status-badge').className   = 'badge badge-ok';
      document.getElementById('status-badge').textContent = 'Connecté';
      document.getElementById('footer-ts').textContent    = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
    })
    .catch(function() {
      document.getElementById('status-badge').className   = 'badge badge-warn';
      document.getElementById('status-badge').textContent = 'Erreur';
      document.getElementById('footer-ts').textContent    = 'Erreur de connexion';
    });
}
refresh();
setInterval(refresh, 10000);

document.getElementById('restore-btn').addEventListener('click', function() {
  var input = document.getElementById('restore-file');
  var msg   = document.getElementById('restore-msg');
  if (!input.files || !input.files[0]) {
    msg.textContent = 'Choisissez un fichier de sauvegarde.';
    return;
  }
  var reader = new FileReader();
  reader.onload = function() {
    msg.textContent = 'Restauration en cours...';
    fetch('/api/restore', { method: 'POST', body: reader.result })
      .then(function(r) { return r.json(); })
      .then(function(d) {
        msg.textContent = d.status === 'ok'
          ? 'Restauration réussie - rechargez la page Équipements.'
          : 'Erreur : ' + (d.error || 'inconnue');
      })
      .catch(function() { msg.textContent = 'Erreur de connexion.'; });
  };
  reader.readAsText(input.files[0]);
});
