// lordos.org: the only script on the site (CSP script-src 'self', no inline code).
// Every page works without it; it adds copy buttons, the donation amount, the language
// suggestion on the English home page, and the gallery filters and viewer.
(function () {
  'use strict';
  var d = document;
  var $ = function (s, r) { return (r || d).querySelector(s); };
  var $$ = function (s, r) { return Array.prototype.slice.call((r || d).querySelectorAll(s)); };
  var store = {
    get: function (k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set: function (k, v) { try { localStorage.setItem(k, v); } catch (e) { /* private mode */ } }
  };

  // Copy buttons
  $$('[data-copy]').forEach(function (b) {
    if (!navigator.clipboard) return;
    b.hidden = false;
    var label = b.textContent;
    b.addEventListener('click', function () {
      var src = d.getElementById(b.getAttribute('data-copy'));
      navigator.clipboard.writeText(src.textContent.trim()).then(function () {
        b.textContent = b.getAttribute('data-done');
        setTimeout(function () { b.textContent = label; }, 1800);
      });
    });
  });

  // Donations
  var form = $('#donate');
  if (form) {
    var custom = $('#custom-amount');
    var out = $('#pay-amount');
    var radios = $$('input[name="amount"]', form);
    var wrap = custom.closest('.custom');
    var customValue = function () { var v = parseFloat(custom.value); return v > 0 ? Math.round(v * 100) / 100 : 0; };
    var current = function () {
      if (wrap.classList.contains('on')) return customValue();
      var r = radios.filter(function (x) { return x.checked; })[0];
      return r ? +r.value : 0;
    };
    var sync = function () { var a = current(); out.textContent = a ? String(a) : '—'; };
    radios.forEach(function (r) {
      r.addEventListener('change', function () { wrap.classList.remove('on'); sync(); });
    });
    custom.addEventListener('input', function () {
      wrap.classList.add('on');
      radios.forEach(function (r) { r.checked = false; });
      sync();
    });
    form.addEventListener('submit', function (e) {
      e.preventDefault();
      var a = current();
      location.href = 'https://www.paypal.com/donate/?business=paypal%40iperfex.com&currency_code=USD' + (a ? '&amount=' + a : '');
    });
    sync();
  }

  // Suggest Spanish or Portuguese on the first visit to the English home page. Never redirects.
  var bar = $('#lbar');
  if (bar && location.pathname === '/' && !store.get('lordos-lang-seen')) {
    var nl = (navigator.language || '').toLowerCase().slice(0, 2);
    var opt = bar.getAttribute('data-' + nl);
    store.set('lordos-lang-seen', '1');
    if (opt) {
      var o = JSON.parse(opt);
      bar.setAttribute('lang', nl);
      $('p', bar).textContent = o.text;
      var a = $('a', bar); a.textContent = o.go; a.href = o.href; a.hreflang = nl;
      var x = $('button', bar); x.textContent = o.close;
      x.addEventListener('click', function () { bar.hidden = true; });
      bar.hidden = false;
    }
  }

  // Theme toggle: Auto -> Light -> Dark. theme.js already applied the saved choice before paint.
  var tb = $('.theme-btn');
  if (tb) {
    var root = d.documentElement;
    var modes = ['auto', 'light', 'dark'];
    var label = function () {
      var m = root.getAttribute('data-theme') || 'auto';
      $('.tl', tb).textContent = tb.getAttribute('data-' + m);
      tb.setAttribute('aria-label', tb.getAttribute('data-label') + ': ' + tb.getAttribute('data-' + m));
      $$('svg', tb).forEach(function (s) { s.style.display = s.getAttribute('data-m') === m ? '' : 'none'; });
    };
    tb.addEventListener('click', function () {
      var m = modes[(modes.indexOf(root.getAttribute('data-theme') || 'auto') + 1) % 3];
      if (m === 'auto') root.removeAttribute('data-theme'); else root.setAttribute('data-theme', m);
      store.set('lordos-theme', m);
      label();
    });
    label();
  }

  // Filters (gallery: category + appearance; apps: category). Items carry data-item.
  var items = $$('[data-item]');
  var more = $('#g-more');
  var PAGE = 48;
  var state = { cat: 'all', theme: 'all', limit: PAGE };
  var empty = $('#g-empty');
  var apply = function () {
    var shown = 0, matching = 0;
    items.forEach(function (f) {
      var t = f.getAttribute('data-theme');
      var ok = (state.cat === 'all' || f.getAttribute('data-cat') === state.cat) &&
               (state.theme === 'all' || !t || t === state.theme);
      if (ok) matching++;
      if (ok && more && shown >= state.limit) ok = false;
      if (ok) shown++;
      f.hidden = !ok;
    });
    if (empty) empty.hidden = matching > 0;
    if (more) more.hidden = shown >= matching;
  };
  var group = function (sel, key) {
    var btns = $$(sel);
    btns.forEach(function (b) {
      b.addEventListener('click', function () {
        state[key] = b.getAttribute('data-' + key);
        state.limit = PAGE;
        btns.forEach(function (x) { x.setAttribute('aria-pressed', String(x === b)); });
        apply();
      });
    });
  };
  if (items.length) {
    group('.g-cats .g-chip', 'cat');
    group('.g-themes .g-chip', 'theme');
    if (more) $('button', more).addEventListener('click', function () { state.limit += PAGE; apply(); });
    apply();
  }

  // Lightbox over every visible screenshot link (.g-card)
  var lb = $('#lightbox');
  if (!lb || typeof lb.showModal !== 'function') return; // the links still open the full image
  var img = $('#lb-img'), cap = $('#lb-cap'), pos = $('#lb-pos');
  var list = [], idx = 0, opener = null;
  var show = function (i) {
    var n = list.length;
    idx = (i + n) % n;
    var a = list[idx];
    img.src = a.getAttribute('href');
    img.width = +a.getAttribute('data-w'); img.height = +a.getAttribute('data-h');
    img.alt = a.getAttribute('data-cap');
    cap.textContent = a.getAttribute('data-cap');
    pos.textContent = (idx + 1) + ' / ' + n;
    lb.setAttribute('aria-label', cap.textContent);
  };
  d.addEventListener('click', function (e) {
    var a = e.target.closest && e.target.closest('a.g-card');
    if (!a || e.metaKey || e.ctrlKey || e.shiftKey || e.button) return;
    e.preventDefault();
    list = $$('a.g-card').filter(function (x) { return x.offsetParent !== null; });
    opener = a;
    show(list.indexOf(a));
    lb.showModal();
  });
  lb.addEventListener('click', function (e) {
    var b = e.target.closest('[data-lb]');
    if (b) {
      var k = b.getAttribute('data-lb');
      if (k === 'close') lb.close(); else show(idx + (k === 'next' ? 1 : -1));
    } else if (e.target === lb) {
      lb.close();
    }
  });
  lb.addEventListener('keydown', function (e) {
    if (e.key === 'ArrowRight') { show(idx + 1); e.preventDefault(); }
    else if (e.key === 'ArrowLeft') { show(idx - 1); e.preventDefault(); }
  });
  lb.addEventListener('close', function () {
    var a = list[idx] || opener;
    if (a) a.focus();
  });
})();
