// Empaqueta un tema React (con el SDK y todas las apps de apps/) en un único .js que
// lord-server carga con QuickJS.
// Uso: node tools/build-theme.mjs [tema ...]   (sin argumentos: todos los temas)
import { build } from 'esbuild';
import { fileURLToPath } from 'node:url';
import fs from 'node:fs';
import path from 'node:path';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));

// Módulo virtual '@lordos/apps': una entrada por cada apps/<id>/manifest.json.
const appsPlugin = {
  name: 'lordos-apps',
  setup(b) {
    b.onResolve({ filter: /^@lordos\/apps$/ }, () => ({ path: 'apps', namespace: 'lordos-apps' }));
    b.onLoad({ filter: /.*/, namespace: 'lordos-apps' }, () => {
      const dir = path.join(root, 'apps');
      const ids = fs.readdirSync(dir).filter((d) => fs.existsSync(path.join(dir, d, 'manifest.json'))).sort();
      const lines = ids.map((id, i) => {
        const base = path.join(dir, id);
        return `import * as A${i} from ${JSON.stringify(path.join(base, 'index.jsx'))};\n` +
               `import M${i} from ${JSON.stringify(path.join(base, 'manifest.json'))};`;
      });
      const list = ids.map((_, i) => `{ ...M${i}, Component: A${i}.default, Icon: A${i}.Icon }`).join(',\n  ');
      return { contents: `${lines.join('\n')}\nexport const apps = [\n  ${list}\n];\n`, resolveDir: root, loader: 'js' };
    });
  },
};

async function buildTheme(name) {
  const dir = path.join(root, 'themes', name);
  const t0 = Date.now();
  const result = await build({
    entryPoints: [path.join(dir, 'index.jsx')],
    outfile: path.join(dir, 'theme.bundle.js'),
    bundle: true,
    format: 'iife',
    target: 'es2020',
    minify: true,
    jsx: 'automatic',
    alias: {
      '@lordos/runtime': path.join(root, 'runtime', 'index.js'),
      '@lordos/sdk': path.join(root, 'sdk', 'index.js'),
    },
    plugins: [appsPlugin],
    define: { 'process.env.NODE_ENV': '"production"' },
    metafile: true,
    logLevel: 'warning',
  });
  const bytes = Object.values(result.metafile.outputs)[0].bytes;
  console.log(`tema ${name}: ${(bytes / 1024).toFixed(0)} KB en ${Date.now() - t0} ms`);
}

const names = process.argv.slice(2);
// El «app host»: React + runtime + SDK (broker) + el cargador de la app, para lord-server --app
// (ver server/src/app/appclient.c). Un solo bundle en assets/app-host.bundle.js.
async function buildAppHost() {
  const t0 = Date.now();
  const result = await build({
    entryPoints: [path.join(root, 'runtime', 'app-host.js')],
    outfile: path.join(root, 'assets', 'app-host.bundle.js'),
    bundle: true, format: 'iife', target: 'es2020', minify: true, jsx: 'automatic',
    alias: { '@lordos/runtime': path.join(root, 'runtime', 'index.js'), '@lordos/sdk': path.join(root, 'sdk', 'index.js') },
    define: { 'process.env.NODE_ENV': '"production"' }, metafile: true, logLevel: 'warning',
  });
  const bytes = Object.values(result.metafile.outputs)[0].bytes;
  console.log(`app-host: ${(bytes / 1024).toFixed(0)} KB en ${Date.now() - t0} ms`);
}

const all = fs.readdirSync(path.join(root, 'themes'))
  .filter((d) => fs.existsSync(path.join(root, 'themes', d, 'index.jsx')));
for (const name of (names.length ? names : all).filter((n) => n !== 'app-host')) await buildTheme(name);
if (!names.length || names.includes('app-host')) await buildAppHost();
