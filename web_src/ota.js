// Récupère la version au chargement
fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
  document.getElementById('footer-ts').textContent =
    'Firmware actuel : v' + (d.version || '—');
}).catch(function() {});

document.getElementById('f').addEventListener('submit', function(e) {
  e.preventDefault();
  var file = document.getElementById('fw').files[0];
  if (!file) {
    document.getElementById('msg').textContent = 'Aucun fichier sélectionné.';
    return;
  }
  var fd = new FormData();
  fd.append('firmware', file);
  var xhr = new XMLHttpRequest();
  xhr.upload.onprogress = function(e) {
    document.getElementById('pg').style.display = 'block';
    document.getElementById('bar').style.width = (e.loaded / e.total * 100) + '%';
    document.getElementById('msg').textContent =
      'Transfert : ' + Math.round(e.loaded / e.total * 100) + '%';
  };
  xhr.onload = function() {
    var m = document.getElementById('msg');
    if (xhr.status === 200) {
      m.style.color = '#10b981';
      m.textContent = 'Firmware transféré — redémarrage en cours…';
      document.getElementById('f').style.display = 'none';
      waitForReboot();
    } else {
      m.style.color = '#ef4444';
      m.textContent = 'Erreur : ' + xhr.responseText;
    }
  };
  xhr.open('POST', '/update');
  xhr.send(fd);
});

function waitForReboot() {
  var m = document.getElementById('msg');
  var dot = 0;
  var dots = ['', '.', '..', '...'];
  setTimeout(function poll() {
    dot = (dot + 1) % 4;
    m.textContent = 'Redémarrage en cours' + dots[dot];
    fetch('/api/status', { cache: 'no-store' })
      .then(function(r) {
        if (r.ok) {
          m.textContent = 'Redémarrage terminé — redirection…';
          setTimeout(function() { window.location.href = '/'; }, 800);
        } else {
          setTimeout(poll, 1000);
        }
      })
      .catch(function() { setTimeout(poll, 1000); });
  }, 3000);
}
