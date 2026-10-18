// @lordos/sdk — todo lo que necesita un tema o una app de LordOS.
import { createElement, cloneElement, Fragment } from 'react';
import { render, onScreenChange, addListener } from '@lordos/runtime';
import { settings } from './settings.js';
import { createStore, useStore } from './store.js';
import { locale, tOptional } from './i18n.js';

export { View, Text, Image, Terminal, Surface, MediaView, screen, addListener, useClock, system, layouts, getLayout, modifiers } from '@lordos/runtime';
export { createStore, useStore } from './store.js';
export { settings, useSettings } from './settings.js';
export { t, tOptional, useLocale, locale, languages, appName, formatDate, formatTime, formatNumber, weekdayName, setLanguage, reloadCatalogs, direction, textDirection } from './i18n.js';
export { keyboard } from './keyboard.js';
export { trackpad, useTrackpads, hasTouchpad, registerGestureActions, lastSwipe, TRACKPAD_DEFAULTS, trackpadConfig, setTrackpad } from './trackpad.js';
export { ime, useIme } from './ime.js';
export { wayland } from './wayland.js';
export { useLauncherApps, launchApp, refreshApps, isLinuxApp, linuxAppFor, LetterIcon, iconFromFile, APP_CATEGORIES, appCategory } from './apps.js';
import { windowIcon as windowIconOf } from './apps.js';
import { ImeOverlay } from './ime.js';
import { ClipboardPanel, ClipboardToast, clipboard as clipboardApi } from './clipboard.js';
import { SpotlightPanel } from './spotlight.js';
import { RemoteBadge } from './remote.js';
export { remote, remoteViewers } from './remote.js';
export { perms, permsRev, requestPermission, markDeviceUse, useIndicators, PrivacyIndicators, PermissionPrompt, registerPrivacySection, privacySections, PERMISSIONS, onPermissionRevoked, micStart, micStop, micLevel } from './privacy.js';
import { PermissionPrompt } from './privacy.js';
import { Powerbox } from './powerbox.jsx';
export { openFile, saveFile, Powerbox } from './powerbox.jsx';
// documentos e impresión (M9): PDF, comprimidos, fuentes, diálogo de impresión y emojis
export { docAvailable, DocImage, releaseImage, releaseBlob, saveBlob, fontInfo, fontSample, fontGrid, decodeImage, convertImage, makeTextPdf, makeImagePdf, pdfThumb, warmDocs } from './doc.js';
export { pdf } from './pdf.js';
export { archive } from './archive.js';
export { print, queueName } from './print.js';
export { emoji, Grid as EmojiGrid, ToneBar as EmojiToneBar } from './emoji.jsx';
import { PrintDialog } from './printdialog.jsx';
import { EmojiPanel } from './emoji.jsx';
export { admin } from './admin.js';
export { logs } from './logs.js';
export { LordOSLogo } from './logo.js';
export { DesktopWidgets, registerWidgetType, setWidgets, desktopWidgets } from './widgets.js';
export { Markdown, markdownHeadings, parseMarkdown, parseInline, toggleTask } from './markdown.jsx';
export { dock, useDockEntries, dockActivate, dockMenu, registerDockMenu, launcherIdOf, dockPrefs, DOCK_DEFAULTS } from './dock.js';
export { sound, useSound, useSoundPeak, soundState, defaultSink, defaultSource, microphones } from './sound.js';
export { monitor, useMonitor, monitorSamples, monitorProcesses, monitorConnections, monitorDevices, MONITOR_HISTORY, firewall, firewallState } from './monitor.js';
export { packages, packageList, refreshPackages, packageFile, loadWasm } from './packages.js';
import { refreshPackages } from './packages.js';
export {
  windows, wm, useWindows, configureWindows, getApp, listApps, isLiveSystem, appIdOf, workArea, appMenus, useAppMenus, windowTitle, appRegistry, appMenuClick,
} from './windows.js';
export { WindowShell, WINDOW_EDGE } from './windowshell.jsx';
export { term } from './terminal.js';
export { clipboard, clipboardHistory, clipboardPanel, clipboardKind } from './clipboard.js';
export { spotlight, spotlightOpen, calc as spotlightCalc, convert as spotlightConvert } from './spotlight.js';
export { registerSpotlightSource } from './spotlight.js';
// avisos de la sesión, íconos de estado de la barra, Reloj (alarmas) y Ayuda (docs/SISTEMA.md)
export { notify, dismissNotification, useNotifications, notifications, notifyHistory, clearNotifyHistory, NotificationBanner } from './notify.js';
export { registerStatusItem, StatusItems, statusItems } from './statusitems.js';
export { clockAlarms, clockTimer, timerStart, timerPause, timerResume, timerReset, timerLeft, stopwatch, swStart, swStop, swLap, swReset, swElapsed, nextAlarm, nextFire } from './alarms.js';
export { help } from './help.js';
import { NotificationBanner } from './notify.js';
import { startAlarms } from './alarms.js';
import { maybeStartTour } from './help.js';
import { getApp as getAppOf, isLiveSystem as isLiveSystemOf, wm as wmOf } from './windows.js';
export const windowIcon = (w) => windowIconOf(w, getAppOf);
export { systemInfo, useSystemInfo, useServerStats, useCpuUsage, formatBytes, formatDuration } from './system.js';
export * as fs from './fs.js';
export { useDirectory } from './fs.js';
export { ThemeProvider, useTheme, DEFAULT_TOKENS } from './theme.js';
export {
  Button, Toggle, Segmented, SidebarItem, SectionTitle, ScrollView, TextInput, hasTextFocus, takeTextFocus, releaseTextFocus, textFocusOwner, Stepper, Slider,
  AppHost, useAppWindow, useKeyboard, focusToken, restoreFocus, pasteIntoFocused, FloatingWindows, useWindowState,
} from './components.jsx';
import { FloatingWindows } from './components.jsx';
export { media, useMedia, usePlayer, nowPlaying, useNowPlaying, NowPlayingTile, mediaFormatTime, thumbFor, useThumb, userDirs } from './media.js';
export { disks, useDisks } from './disks.js';
export { backup, useBackup, backupStatus } from './backup.js';   // Copias de seguridad (docs/SISTEMA.md)
export { vaultAvailable, vaultRequest, onVaultEvent, keyring, autofill, autofillReveal, KeyringPrompt } from './vault.js';   // Contraseñas y llavero (docs/CONTRASENAS.md)
import { KeyringPrompt } from './vault.js';

// ---- temas instalados ----

export const themes = {
  list: () => lord.themes.list().map((t) => {
    let meta = {};
    try { meta = t.meta ? JSON.parse(t.meta) : {}; } catch (e) { /* theme.json inválido */ }
    // nombre y descripción traducidos (shell.theme.<id>.*) o los de theme.json
    return { id: t.id, current: t.current, hidden: !!meta.hidden, name: tOptional(`shell.theme.${t.id}.name`) || meta.name || t.id,
      description: tOptional(`shell.theme.${t.id}.description`) || meta.description || '' };
  }).filter((t) => !t.hidden),         // la pantalla de ingreso (themes/greeter) no se elige
  current: () => lord.themes.list().find((t) => t.current)?.id || null,
  // Cambia de tema en vivo: el servidor descarta este contexto JS y carga el nuevo.
  use(id) {
    settings.set({ theme: id });
    lord.settings.save(JSON.stringify(settings.get(), null, 2));
    return lord.themes.use(id);
  },
};

// Sesión: cerrar, bloquear la pantalla o volver a la pantalla de ingreso para que entre otro
// usuario sin cerrar esta (docs/SESIONES.md). Bloquear y cambiar de usuario necesitan
// lord-sessiond (managed()); en una sesión vista solo desde el navegador, bloquear cierra el
// acceso del navegador.
export const session = {
  quit: () => { clipboardApi.beforeLogout(); lord.quit(); },
  managed: () => typeof lord.session !== 'undefined' && lord.session.managed(),
  lock: () => typeof lord.session !== 'undefined' && lord.session.lock(),
  switchUser: () => typeof lord.session !== 'undefined' && lord.session.switchUser(),
};

// ❖L o ❖Ctrl+Q bloquean la pantalla (como en macOS).
addListener('keydown', (e) => {
  const k = String(e.base || e.key || '').toLowerCase();
  if (e.meta && !e.alt && !e.repeat && ((k === 'l' && !e.ctrl && !e.shift) || (k === 'q' && e.ctrl)) && session.managed()) session.lock();
});

// Cambió el tamaño de la pantalla: se vuelve a dibujar todo con las medidas nuevas.
const screenRev = createStore(0);
onScreenChange(() => screenRev.set((v) => v + 1));

// Punto de entrada de un tema: respeta el tema elegido por el usuario.
export function startShell(element) {
  const wanted = settings.get().theme;
  const current = themes.current();
  if (wanted && current && wanted !== current && lord.themes.use(wanted)) return;
  refreshPackages();                    // después de configureWindows del tema
  render(createElement(LocaleRoot, null, element));
  // ISO live: el asistente de instalación se abre solo, al frente
  if (isLiveSystemOf() && getAppOf('installer')) setTimeout(() => wmOf.open('installer'), 300);
  startAlarms();                        // alarmas y temporizador del Reloj, con la app cerrada
  maybeStartTour();                     // «Bienvenido a LordOS», la primera vez
}

// Al cambiar el idioma se vuelve a montar toda la interfaz (las ventanas, pestañas y
// sesiones viven en stores y no se pierden).
function LocaleRoot({ children }) {
  const { lang, region } = useStore(locale);
  const rev = useStore(screenRev);
  return createElement(Fragment, { key: `${lang}|${region}` }, rev ? cloneElement(children, { screenRev: rev }) : children, createElement(ClipboardPanel), createElement(SpotlightPanel), createElement(ClipboardToast), createElement(RemoteBadge), createElement(ImeOverlay), createElement(FloatingWindows), createElement(PermissionPrompt), createElement(EmojiPanel), createElement(PrintDialog), createElement(Powerbox), createElement(NotificationBanner), createElement(KeyringPrompt));
}
