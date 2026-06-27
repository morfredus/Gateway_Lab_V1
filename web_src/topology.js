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

function isAccessPointLike(d) {
  return d.category === 'Router' ||
    (d.type && d.type.toLowerCase().indexOf('répéteur') >= 0) ||
    (d.type && d.type.toLowerCase().indexOf('point d\'accès') >= 0);
}

// Choix automatique de la racine quand aucune n'est forcee par l'utilisateur :
// la box operateur (categorie "Router", deduite par IspDetector/SSDP/OUI) —
// jamais l'ESP32 lui-meme (categorie "Gateway" : un equipement du reseau
// comme un autre du point de vue de la topologie, pas la racine).
function autoPickRoot(devices) {
  return devices.find(function(d) { return d.category === 'Router'; }) ||
         devices.find(function(d) { return d.category === 'Gateway'; }) ||
         devices[0];
}

function resolveRoot(devices, forcedRootMac) {
  if (forcedRootMac) {
    var forced = devices.find(function(d) { return d.mac === forcedRootMac; });
    if (forced) return forced;
  }
  return autoPickRoot(devices);
}

// Construit l'arbre a partir des rattachements declares (topologyParent).
// A defaut de declaration, un equipement est rattache directement a la
// racine (un scan ARP/SSDP seul ne sait pas dire a quel repeteur WiFi
// precis il est connecte).
function buildTree(devices, root) {
  var byMac = {};
  devices.forEach(function(d) { if (d.mac) byMac[d.mac] = d; });

  var nodes = {};
  devices.forEach(function(d) { nodes[d.mac || d.ip] = { device: d, children: [] }; });

  var rootKey = root.mac || root.ip;
  var roots = [nodes[rootKey]];

  devices.forEach(function(d) {
    var key = d.mac || d.ip;
    if (key === rootKey) return;

    var parentKey = (d.topologyParent && byMac[d.topologyParent]) ? d.topologyParent : rootKey;
    if (nodes[parentKey] && parentKey !== key) {
      nodes[parentKey].children.push(nodes[key]);
    } else {
      roots.push(nodes[key]);
    }
  });

  return { roots: roots, nodesByMac: nodes, rootKey: rootKey };
}

// Ensemble des MAC descendantes d'un noeud (pour interdire un depot qui
// creerait un cycle pendant le glisser-deposer).
function descendantMacs(node) {
  var out = {};
  function walk(n) {
    n.children.forEach(function(c) {
      var k = c.device.mac || c.device.ip;
      out[k] = true;
      walk(c);
    });
  }
  walk(node);
  return out;
}

function nodeColor(d, isRoot) {
  if (isRoot)                   return { fill: '#0c4a6e', stroke: '#38bdf8', text: '#e0f2fe' };
  if (isAccessPointLike(d))     return { fill: '#1c2a1a', stroke: '#86efac', text: '#dcfce7' };
  if (d.topologyParentAuto) {
    var conf = d.topologyParentConfidence || 0;
    if (conf >= 60) return { fill: '#1e2a4a', stroke: '#60a5fa', text: '#dbeafe' };   // place automatiquement, confiance elevee
    return            { fill: '#3a2a14', stroke: '#f59e0b', text: '#fef3c7' };          // place automatiquement, confiance faible/ambigu
  }
  return { fill: '#1e293b', stroke: '#334155', text: '#e2e8f0' };
}

var ROW_H = 50, COL_W = 230, BOX_W = 200, BOX_H = 34;

// Rendu SVG fait maison : disposition en arbre vertical (profondeur =
// colonne, ordre de visite = ligne), traits en L entre un noeud et son
// parent. Retourne la disposition calculee (utilisee ensuite pour le
// glisser-deposer).
function renderSvg(tree) {
  var svg = document.getElementById('topology-svg');
  var rows = [];
  var rowIndex = {};

  function visit(node, depth) {
    var idx = rows.length;
    rows.push({ node: node, depth: depth });
    rowIndex[node.device.mac || node.device.ip] = idx;
    node.children.forEach(function(c) { visit(c, depth + 1); });
  }
  tree.roots.forEach(function(r) { visit(r, 0); });

  var width  = (rows.reduce(function(m, r) { return Math.max(m, r.depth); }, 0) + 1) * COL_W + 20;
  var height = rows.length * ROW_H + 20;
  svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
  svg.setAttribute('width', width);
  svg.setAttribute('height', height);

  var html = '';
  var layout = [];   // [{mac, x, y, w, h}] pour le hit-test du drag&drop
  rows.forEach(function(r, i) {
    var x = r.depth * COL_W + 10;
    var y = i * ROW_H + 10;
    var d = r.node.device;
    var key = d.mac || d.ip;
    var isRoot = key === tree.rootKey;
    var c = nodeColor(d, isRoot);
    var label = esc(deviceLabel(d));
    var sub = esc(d.type || d.category || d.manufacturer || '');
    if (d.topologyParentAuto && !isRoot) sub += ' · auto ' + (d.topologyParentConfidence || 0) + '%';

    if (r.depth > 0) {
      var pIdx = null;
      for (var k = i - 1; k >= 0; k--) { if (rows[k].depth === r.depth - 1) { pIdx = k; break; } }
      if (pIdx !== null) {
        var px = rows[pIdx].depth * COL_W + 10 + BOX_W;
        var py = pIdx * ROW_H + 10 + BOX_H / 2;
        var cx = x;
        var cy = y + BOX_H / 2;
        var midX = px + 16;
        var auto = !!d.topologyParentAuto;
        var dash = auto ? ' stroke-dasharray="4 3"' : '';
        html += '<path d="M' + px + ' ' + py + ' L' + midX + ' ' + py + ' L' + midX + ' ' + cy + ' L' + cx + ' ' + cy + '" fill="none" stroke="#334155" stroke-width="1.5"' + dash + '/>';
      }
    }

    html += '<g class="topo-node' + (isRoot ? ' topo-root-node' : '') + '" data-mac="' + esc(d.mac || '') + '">' +
      '<rect x="' + x + '" y="' + y + '" width="' + BOX_W + '" height="' + BOX_H + '" rx="7" fill="' + c.fill + '" stroke="' + c.stroke + '"/>' +
      '<text x="' + (x + 10) + '" y="' + (y + 14) + '" fill="' + c.text + '" font-size="12" font-weight="700">' +
        (label.length > 24 ? label.slice(0, 23) + '…' : label) + '</text>' +
      '<text x="' + (x + 10) + '" y="' + (y + 27) + '" fill="#9aacc2" font-size="10">' +
        (sub.length > 28 ? sub.slice(0, 27) + '…' : sub) + (d.ip ? ' · ' + esc(d.ip) : '') + '</text>' +
      '</g>';

    layout.push({ mac: d.mac, x: x, y: y, w: BOX_W, h: BOX_H, isRoot: isRoot });
  });

  svg.innerHTML = html;
  return layout;
}

function setDragMsg(text, isError) {
  var el = document.getElementById('topo-drag-msg');
  el.textContent = text || '';
  el.className = 'topo-drag-msg' + (isError ? ' topo-drag-error' : '');
}

// Glisser-deposer : deplace visuellement le noeud (transform) pendant le
// drag, puis au relachement determine la cible sous le pointeur par
// hit-test sur la disposition calculee. Aucune lib externe — coordonnees
// SVG converties depuis les coordonnees ecran via le ratio viewBox/rect.
function attachDragAndDrop(svg, layout, tree, onDrop) {
  var drag = null;

  function toSvgPoint(evt) {
    var rect = svg.getBoundingClientRect();
    var vb = svg.viewBox.baseVal;
    var t = evt.touches && evt.touches[0] ? evt.touches[0] : evt;
    return {
      x: (t.clientX - rect.left) * (vb.width  / rect.width),
      y: (t.clientY - rect.top)  * (vb.height / rect.height)
    };
  }

  function hitTest(pt, excludeMacs) {
    for (var i = layout.length - 1; i >= 0; i--) {
      var n = layout[i];
      if (excludeMacs[n.mac]) continue;
      if (pt.x >= n.x && pt.x <= n.x + n.w && pt.y >= n.y && pt.y <= n.y + n.h) return n;
    }
    return null;
  }

  function clearHighlight() {
    svg.querySelectorAll('.topo-node.topo-drop-target').forEach(function(g) { g.classList.remove('topo-drop-target'); });
  }

  function onMove(evt) {
    if (!drag) return;
    evt.preventDefault();
    var pt = toSvgPoint(evt);
    drag.el.setAttribute('transform', 'translate(' + (pt.x - drag.offsetX - drag.originX) + ',' + (pt.y - drag.offsetY - drag.originY) + ')');
    clearHighlight();
    var target = hitTest(pt, drag.forbidden);
    if (target) {
      var g = svg.querySelector('.topo-node[data-mac="' + target.mac.replace(/"/g, '') + '"]');
      if (g) g.classList.add('topo-drop-target');
    }
  }

  function onUp(evt) {
    if (!drag) return;
    var pt = toSvgPoint(evt);
    var target = hitTest(pt, drag.forbidden);
    drag.el.removeAttribute('transform');
    clearHighlight();
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('mouseup', onUp);
    document.removeEventListener('touchmove', onMove);
    document.removeEventListener('touchend', onUp);
    var draggedMac = drag.mac;
    drag = null;
    if (target) onDrop(draggedMac, target.mac);
  }

  svg.querySelectorAll('.topo-node').forEach(function(g) {
    var mac = g.getAttribute('data-mac');
    if (!mac || g.classList.contains('topo-root-node')) return;   // racine fixe (selecteur dedie)

    var node = tree.nodesByMac[mac];
    var forbidden = node ? descendantMacs(node) : {};
    forbidden[mac] = true;   // ne peut pas se deposer sur soi-meme

    function start(evt) {
      evt.preventDefault();
      var pt = toSvgPoint(evt);
      var entry = layout.find(function(n) { return n.mac === mac; });
      drag = { mac: mac, el: g, originX: entry.x, originY: entry.y, offsetX: pt.x - entry.x, offsetY: pt.y - entry.y, forbidden: forbidden };
      g.classList.add('topo-dragging');
      document.addEventListener('mousemove', onMove);
      document.addEventListener('mouseup', onUp);
      document.addEventListener('touchmove', onMove, { passive: false });
      document.addEventListener('touchend', onUp);
    }
    g.addEventListener('mousedown', start);
    g.addEventListener('touchstart', start, { passive: false });
  });
}

function renderRootSelect(devices, forcedRootMac, resolvedRoot) {
  var sel = document.getElementById('topo-root-select');
  var current = document.getElementById('topo-root-current');
  var candidates = devices.filter(function(d) { return d.mac; });

  sel.innerHTML = '<option value="">Automatique (box opérateur)</option>' +
    candidates.map(function(d) {
      var selAttr = (forcedRootMac === d.mac) ? ' selected' : '';
      return '<option value="' + esc(d.mac) + '"' + selAttr + '>' + esc(deviceLabel(d)) + '</option>';
    }).join('');

  current.textContent = forcedRootMac ? '' : ('Actuellement : ' + deviceLabel(resolvedRoot));

  sel.onchange = function() {
    var params = new URLSearchParams({ mac: sel.value });
    fetch('/api/topology/root', { method: 'POST', body: params })
      .then(function() { return loadAndRender(); })
      .catch(function() { setDragMsg('Erreur réseau lors du changement de racine', true); });
  };
}

function renderTopology(devices, forcedRootMac) {
  var empty = document.getElementById('topology-empty');
  var wrap  = document.querySelector('.topo-wrap');
  if (!devices || !devices.length) {
    wrap.style.display = 'none';
    empty.style.display = 'block';
    document.getElementById('topo-root-select').innerHTML = '';
    return;
  }
  wrap.style.display = 'block';
  empty.style.display = 'none';

  var root = resolveRoot(devices, forcedRootMac);
  renderRootSelect(devices, forcedRootMac, root);

  var tree = buildTree(devices, root);
  var svg = document.getElementById('topology-svg');
  var layout = renderSvg(tree);

  attachDragAndDrop(svg, layout, tree, function(draggedMac, targetMac) {
    var params = new URLSearchParams({ mac: draggedMac, parent: targetMac });
    fetch('/api/topology/parent', { method: 'POST', body: params })
      .then(function(r) { return r.json(); })
      .then(function(j) {
        if (j.status === 'ok') setDragMsg('Rattachement mis à jour.', false);
        else setDragMsg(j.error || 'Erreur lors du rattachement', true);
        return loadAndRender();
      })
      .catch(function() { setDragMsg('Erreur réseau lors du rattachement', true); });
  });
}

function loadAndRender() {
  return fetch('/api/devices')
    .then(function(r) { return r.json(); })
    .then(function(data) {
      renderTopology(data.devices, data.topologyRoot || '');
      document.getElementById('footer-ts').textContent = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
    })
    .catch(function() {});
}

loadAndRender();
