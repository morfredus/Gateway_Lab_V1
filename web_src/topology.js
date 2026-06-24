fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
}).catch(function() {});

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function deviceLabel(d) {
  return d.alias || d.hostnameDisplay || d.hostname || d.ip;
}

// Un equipement est considere "point d'acces / repeteur" s'il est detecte
// comme tel (type rempli par applyMeshDetection cote firmware, ex. TP-Link
// Deco) ou classe Router/Gateway par le scan.
function isAccessPointLike(d) {
  return d.category === 'Gateway' || d.category === 'Router' ||
    (d.type && d.type.toLowerCase().indexOf('répéteur') >= 0) ||
    (d.type && d.type.toLowerCase().indexOf('point d\'accès') >= 0);
}

// Construit l'arbre : la passerelle (ou a defaut le premier routeur) est la
// racine. Le parent declare manuellement (topologyParent) est toujours
// prioritaire ; en son absence, tout equipement non-AP est rattache par
// defaut a la passerelle (le scan ARP seul ne sait pas dire a quel
// repeteur WiFi precis un appareil est connecte).
function buildTree(devices) {
  var byMac = {};
  devices.forEach(function(d) { if (d.mac) byMac[d.mac] = d; });

  var gateway = devices.find(function(d) { return d.category === 'Gateway'; }) ||
                devices.find(function(d) { return d.category === 'Router'; });

  var nodes = {};
  devices.forEach(function(d) {
    nodes[d.mac || d.ip] = { device: d, children: [] };
  });

  var roots = [];
  devices.forEach(function(d) {
    var key = d.mac || d.ip;
    if (d === gateway) { roots.push(nodes[key]); return; }

    var parentKey = (d.topologyParent && byMac[d.topologyParent]) ? d.topologyParent : null;
    if (!parentKey && gateway) parentKey = gateway.mac;

    if (parentKey && nodes[parentKey] && parentKey !== key) {
      nodes[parentKey].children.push(nodes[key]);
    } else {
      roots.push(nodes[key]);
    }
  });

  return roots;
}

function nodeColor(d) {
  if (d.category === 'Gateway') return { fill: '#0c4a6e', stroke: '#38bdf8', text: '#e0f2fe' };
  if (isAccessPointLike(d))     return { fill: '#1c2a1a', stroke: '#86efac', text: '#dcfce7' };
  return { fill: '#1e293b', stroke: '#334155', text: '#e2e8f0' };
}

// Rendu SVG fait maison : disposition en arbre vertical (profondeur = colonne,
// ordre de visite = ligne), traits en L entre un noeud et son parent.
function renderSvg(roots) {
  var svg = document.getElementById('topology-svg');
  var ROW_H = 50, COL_W = 230, BOX_W = 200, BOX_H = 34;
  var rows = [];   // { node, depth }
  var rowIndex = {};

  function visit(node, depth) {
    var idx = rows.length;
    rows.push({ node: node, depth: depth });
    rowIndex[node.device.mac || node.device.ip] = idx;
    node.children.forEach(function(c) { visit(c, depth + 1); });
  }
  roots.forEach(function(r) { visit(r, 0); });

  var width  = Math.max(1, rows.reduce(function(m, r) { return Math.max(m, r.depth); }, 0) + 1) * COL_W + 20;
  var height = rows.length * ROW_H + 20;
  svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
  svg.setAttribute('width', width);
  svg.setAttribute('height', height);

  var html = '';
  rows.forEach(function(r, i) {
    var x = r.depth * COL_W + 10;
    var y = i * ROW_H + 10;
    var d = r.node.device;
    var c = nodeColor(d);
    var label = esc(deviceLabel(d));
    var sub = esc(d.type || d.category || d.manufacturer || '');

    if (r.depth > 0) {
      var parentMac = d.topologyParent && rowIndex[d.topologyParent] !== undefined
        ? d.topologyParent
        : null;
      var pIdx = parentMac !== null ? rowIndex[parentMac] : null;
      // Fallback : si pas de parent explicite resolu par index (cas du
      // rattachement par defaut a la passerelle), relie au noeud du depth-1
      // le plus proche au-dessus dans l'ordre de visite.
      if (pIdx === null) {
        for (var k = i - 1; k >= 0; k--) { if (rows[k].depth === r.depth - 1) { pIdx = k; break; } }
      }
      if (pIdx !== null) {
        var px = rows[pIdx].depth * COL_W + 10 + BOX_W;
        var py = pIdx * ROW_H + 10 + BOX_H / 2;
        var cx = x;
        var cy = y + BOX_H / 2;
        var midX = px + 16;
        html += '<path d="M' + px + ' ' + py + ' L' + midX + ' ' + py + ' L' + midX + ' ' + cy + ' L' + cx + ' ' + cy + '" fill="none" stroke="#334155" stroke-width="1.5"/>';
      }
    }

    html += '<g class="topo-node" data-mac="' + esc(d.mac || '') + '">' +
      '<rect x="' + x + '" y="' + y + '" width="' + BOX_W + '" height="' + BOX_H + '" rx="7" fill="' + c.fill + '" stroke="' + c.stroke + '"/>' +
      '<text x="' + (x + 10) + '" y="' + (y + 14) + '" fill="' + c.text + '" font-size="12" font-weight="700">' +
        (label.length > 24 ? label.slice(0, 23) + '…' : label) + '</text>' +
      '<text x="' + (x + 10) + '" y="' + (y + 27) + '" fill="#9aacc2" font-size="10">' +
        (sub.length > 28 ? sub.slice(0, 27) + '…' : sub) + (d.ip ? ' · ' + esc(d.ip) : '') + '</text>' +
      '</g>';
  });

  svg.innerHTML = html;
}

function renderAssignTable(devices) {
  var body = document.getElementById('topo-assign-body');
  var candidates = devices.filter(function(d) { return d.mac; });

  body.innerHTML = candidates.map(function(d) {
    var options = '<option value="">— Passerelle / direct —</option>' +
      candidates.filter(function(p) { return p.mac !== d.mac; }).map(function(p) {
        var sel = (d.topologyParent === p.mac) ? ' selected' : '';
        return '<option value="' + esc(p.mac) + '"' + sel + '>' + esc(deviceLabel(p)) + '</option>';
      }).join('');
    return '<tr>' +
      '<td>' + esc(deviceLabel(d)) + (d.ip ? ' <span class="card-meta">(' + esc(d.ip) + ')</span>' : '') + '</td>' +
      '<td><select class="topo-parent-select" data-mac="' + esc(d.mac) + '">' + options + '</select></td>' +
      '<td><span class="topo-save-msg" data-mac="' + esc(d.mac) + '"></span></td>' +
      '</tr>';
  }).join('');

  body.querySelectorAll('.topo-parent-select').forEach(function(sel) {
    sel.addEventListener('change', function() {
      var mac = sel.getAttribute('data-mac');
      var msg = body.querySelector('.topo-save-msg[data-mac="' + mac + '"]');
      var params = new URLSearchParams({ mac: mac, parent: sel.value });
      fetch('/api/topology/parent', { method: 'POST', body: params })
        .then(function(r) { return r.json(); })
        .then(function(j) {
          if (msg) { msg.textContent = j.status === 'ok' ? '✓' : (j.error || 'erreur'); }
          loadAndRender();
        })
        .catch(function() { if (msg) msg.textContent = 'erreur réseau'; });
    });
  });
}

function renderTopology(devices) {
  var empty = document.getElementById('topology-empty');
  var wrap  = document.querySelector('.topo-wrap');
  if (!devices || !devices.length) {
    wrap.style.display = 'none';
    empty.style.display = 'block';
    document.getElementById('topo-assign-body').innerHTML = '';
    return;
  }
  wrap.style.display = 'block';
  empty.style.display = 'none';

  var roots = buildTree(devices);
  renderSvg(roots);
  renderAssignTable(devices);
}

function loadAndRender() {
  return fetch('/api/devices')
    .then(function(r) { return r.json(); })
    .then(function(data) {
      renderTopology(data.devices);
      document.getElementById('footer-ts').textContent = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
    })
    .catch(function() {});
}

loadAndRender();
