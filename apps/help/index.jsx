// Ayuda — artículos de ayuda de cada app y función, con búsqueda, y el recorrido «Bienvenido a
// LordOS» del primer ingreso (docs/SISTEMA.md). Los artículos son markdown en
// assets/help/<idioma>/ y los lee el SDK (sdk/help.js → lord.help). Los enlaces settings:<sección>,
// app:<id> y help:<artículo> abren Ajustes, una app u otro artículo.
import {
  View, Text, useTheme, useAppWindow, useLocale, t as tr, help, Markdown, ScrollView, TextInput, SidebarItem, Button,
  getApp, wm, appName, settings,
} from '@lordos/sdk';
import { useEffect, useMemo, useState } from 'react';

export function Icon({ size: s }) {
  return (
    <View style={{ width: s, height: s, borderRadius: s * 0.225, background: 'linear-gradient(180deg, #5ac8fa, #0a60d0)',
      boxShadow: `0 ${s * 0.04}px ${s * 0.12}px rgba(0,0,0,0.35)`, alignItems: 'center', justifyContent: 'center' }}>
      <View style={{ width: s * 0.62, height: s * 0.62, borderRadius: s * 0.31, backgroundColor: 'rgba(255,255,255,0.95)', alignItems: 'center', justifyContent: 'center' }}>
        <Text style={{ fontSize: s * 0.44, fontWeight: 700, color: '#0a60d0', lineHeight: s * 0.62 }}>?</Text>
      </View>
    </View>
  );
}

const SECTIONS = ['start', 'desktop', 'apps', 'system', 'settings'];

// ---- recorrido de bienvenida ----

function TourArt({ step }) {
  const t = useTheme();
  const icons = [['about'], ['files', 'terminal', 'store'], [], ['settings', 'monitor'], ['clock', 'help'], ['help']][step] || [];
  const glyph = ['👋', '', '⌕', '', '', '?'][step];
  return (
    <View style={{ height: 120, flexDirection: 'row', gap: 18, alignItems: 'center', justifyContent: 'center' }}>
      {icons.map((id) => { const a = getApp(id); return a?.Icon ? <a.Icon key={id} size={84} /> : null; })}
      {!icons.length || step === 0 ? <Text style={{ fontSize: 64, color: t.text }}>{glyph}</Text> : null}
    </View>
  );
}

function Tour({ onDone }) {
  const t = useTheme();
  const [step, setStep] = useState(0);
  const steps = 6;
  const last = step === steps - 1;
  const actions = [
    null,
    null,
    null,
    [['settings', 'appearance'], ['settings', 'network'], ['settings', 'users']],
    null,
    null,
  ][step];
  return (
    <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', padding: 40, gap: 18 }}>
      <TourArt step={step} />
      <Text style={{ fontSize: 26, fontWeight: 700, color: t.text, textAlign: 'center' }}>{tr(`help.tour.${step}.title`)}</Text>
      <Text style={{ fontSize: 14, color: t.text, textAlign: 'center', lineHeight: 1.5, maxWidth: 560 }}>{tr(`help.tour.${step}.body`)}</Text>
      {actions && (
        <View style={{ flexDirection: 'row', gap: 8 }}>
          {actions.map(([app, section]) => (
            <Button key={section} label={tr(`settings.section.${section}`)} onClick={() => wm.open(app, { args: { section } })} />
          ))}
        </View>
      )}
      <View style={{ flexDirection: 'row', gap: 6, marginTop: 6 }}>
        {Array.from({ length: steps }, (_, i) => (
          <View key={i} onClick={() => setStep(i)} style={{ width: 8, height: 8, borderRadius: 4, backgroundColor: i === step ? t.accent : t.control }} />
        ))}
      </View>
      <View style={{ flexDirection: 'row', gap: 10, alignItems: 'center' }}>
        {!last && <Button label={tr('help.tour.skip')} onClick={onDone} />}
        {step > 0 && <Button label={tr('help.tour.back')} onClick={() => setStep(step - 1)} />}
        <Button primary label={last ? tr('help.tour.finish') : tr('help.tour.next')} onClick={() => (last ? onDone() : setStep(step + 1))} />
      </View>
    </View>
  );
}

// ---- artículos ----

function Article({ id, onOpen }) {
  const t = useTheme();
  const a = id ? help.read(id) : null;
  if (!a) return <Text style={{ fontSize: 13, color: t.textDim, padding: 24, textAlign: 'start' }}>{tr('help.notFound')}</Text>;
  const app = a.app ? getApp(a.app) : null;
  const onLink = (url) => {
    const m = /^help:([a-z0-9-]+)$/.exec(String(url));
    if (m) { onOpen(m[1]); return; }
    help.link(url);
  };
  return (
    <ScrollView style={{ paddingHorizontal: 28, paddingTop: 18 }}>
      {app && (
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12, marginBottom: 6 }}>
          <app.Icon size={40} />
          <View style={{ flex: 1 }} />
          <Button label={tr('help.openApp', { app: appName(app) })} onClick={() => wm.open(app.id)} />
        </View>
      )}
      <Markdown source={a.body} width={560} onLink={onLink} />
      <View style={{ height: 30 }} />
    </ScrollView>
  );
}

let lastArticle = 'welcome';

export default function Help() {
  const t = useTheme();
  const { lang } = useLocale();
  const { args } = useAppWindow();
  const [article, setArticleState] = useState(lastArticle);
  const [tour, setTour] = useState(false);
  const [query, setQuery] = useState('');
  const setArticle = (id) => { lastArticle = id; setArticleState(id); setTour(false); };
  useEffect(() => {
    if (args?.tour) setTour(true);
    else if (args?.article) setArticle(args.article);
  }, [args?.nonce]);
  const list = useMemo(() => help.list(), [lang]);
  const results = useMemo(() => (query.trim().length >= 2 ? help.search(query) : null), [query, lang]);
  const finishTour = () => { setTour(false); settings.set({ welcomeShown: true }); setArticle('welcome'); };
  if (!help.available() || !list.length) {
    return <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center' }}><Text style={{ fontSize: 13, color: t.textDim }}>{tr('help.unavailable')}</Text></View>;
  }
  return (
    <View style={{ flex: 1, flexDirection: 'row' }}>
      <View style={{ width: 230, backgroundColor: t.sidebarBg, paddingHorizontal: 8, paddingTop: 10 }}>
        <TextInput value={query} onChange={setQuery} placeholder={tr('help.search')} onCancel={() => setQuery('')}
          onSubmit={() => results?.[0] && setArticle(results[0].id)} />
        <View style={{ height: 8 }} />
        <ScrollView style={{ flex: 1 }}>
          {results ? (
            results.length ? results.map((r) => (
              <View key={r.id} onClick={() => setArticle(r.id)} style={{ padding: 8, borderRadius: 7, gap: 2, backgroundColor: r.id === article ? t.accent : 'transparent' }}>
                <Text style={{ fontSize: 13, fontWeight: 600, color: r.id === article ? '#fff' : t.text, textAlign: 'start' }}>{r.title}</Text>
                <Text style={{ fontSize: 11, color: r.id === article ? 'rgba(255,255,255,0.85)' : t.textDim, lineHeight: 1.3, maxHeight: 44, overflow: 'hidden', textAlign: 'start' }}>{r.snippet}</Text>
              </View>
            )) : <Text style={{ fontSize: 12, color: t.textDim, margin: 8, textAlign: 'start' }}>{tr('help.noResults')}</Text>
          ) : (
            <>
              <SidebarItem label={tr('help.tour.open')} selected={tour} onClick={() => setTour(true)}
                icon={<Text style={{ width: 20, fontSize: 14, textAlign: 'center' }}>👋</Text>} />
              {SECTIONS.map((sec) => {
                const items = list.filter((x) => (x.section || 'apps') === sec);
                if (!items.length) return null;
                return (
                  <View key={sec} style={{ marginTop: 10, gap: 1 }}>
                    <Text style={{ fontSize: 10, fontWeight: 700, color: t.textDim, paddingHorizontal: 8, paddingBottom: 3, textAlign: 'start' }}>{tr(`help.section.${sec}`).toUpperCase()}</Text>
                    {items.map((x) => {
                      const app = x.app ? getApp(x.app) : null;
                      return <SidebarItem key={x.id} label={x.title} selected={!tour && x.id === article} onClick={() => setArticle(x.id)}
                        icon={app?.Icon ? <app.Icon size={18} /> : <Text style={{ width: 18, fontSize: 12, color: t.textDim, textAlign: 'center' }}>•</Text>} />;
                    })}
                  </View>
                );
              })}
            </>
          )}
          <View style={{ height: 12 }} />
        </ScrollView>
      </View>
      <View style={{ flex: 1 }}>
        {tour ? <Tour onDone={finishTour} /> : <Article id={article} onOpen={setArticle} />}
      </View>
    </View>
  );
}
