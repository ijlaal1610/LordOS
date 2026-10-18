// @lordos/sdk — everything a LordOS theme or app needs.
import { createElement, cloneElement, Fragment } from 'react';
import { render, onScreenChange, addListener } from '@lordos/runtime';
import { settings } from './settings.js';
import { createStore, useStore } from './store.js';
import { locale, tOptional } from './i18n.js';

export { View, Text, Image, Terminal, Surface, MediaView, screen, addListener, useClock, system, layouts, getLayout, modifiers } from '@lordos/runtime';
export { displays as screenDisplays, mainDisplay, displayAt, displayFor, mainBox } from '@lordos/runtime';
export { createStore, useStore } from './store.js';
export { settings, useSettings } from './settings.js';
export { t, tOptional, useLocale, locale, languages, appName, formatDate, formatTime, formatNumber, weekdayName, monthName, setLanguage, reloadCatalogs, direction, textDirection } from './i18n.js';
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
// documents and printing (M9): PDF, archives, fonts, print dialog and emoji
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
  windows, wm, useWindows, configureWindows, getApp, listApps, isLiveSystem, appIdOf, workArea, workAreaOf, pointerWorkArea, appMenus, useAppMenus, windowTitle, appRegistry, appMenuClick,
  registerBuiltinApp,
} from './windows.js';
export { WindowShell, WINDOW_EDGE } from './windowshell.jsx';
export { term } from './terminal.js';
export { clipboard, clipboardHistory, clipboardPanel, clipboardKind } from './clipboard.js';
export { spotlight, spotlightOpen, calc as spotlightCalc, convert as spotlightConvert } from './spotlight.js';
export { registerSpotlightSource } from './spotlight.js';
// session notifications, menu bar status items, Clock (alarms) and Help (docs/SYSTEM.md)
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
export {
  ToolbarButton, ToolbarGroup, Toolbar, ToolbarSpacer, ToolbarDivider, Hairline, MenuButton, SearchField, EmptyState, GroupBox, FormRow, spacing, typeScale, controlSize,
} from './controls.jsx';
export { media, useMedia, usePlayer, nowPlaying, useNowPlaying, NowPlayingTile, mediaFormatTime, thumbFor, useThumb, userDirs } from './media.js';
export { disks, useDisks } from './disks.js';
export { backup, useBackup, backupStatus } from './backup.js';   // Backups (docs/SYSTEM.md)
export { vaultAvailable, vaultRequest, onVaultEvent, keyring, autofill, autofillReveal, KeyringPrompt, AutofillButton } from './vault.js';   // Passwords and keyring (docs/PASSWORDS.md)
import { KeyringPrompt } from './vault.js';
// Mail, Calendar, Contacts and Reminders through the sandboxed helper lord-maild (docs/MAIL.md)
export { mailAvailable, mailRequest, onMailEvent, mailConfig, mailAccounts, mailStatus, autoconfig, OAUTH_PROVIDERS } from './mail.js';
export { calendars, events as calendarEvents, reminders, usePim, usePimQuery, pimRev, startOfDay } from './calendar.js';
export { FilePicker } from './filepicker.jsx';
export { books as addressBooks, contacts, displayName as contactName, initials as contactInitials, ContactPhoto } from './contacts.js';
import { startMail } from './mail.js';
import { startReminders } from './calendar.js';
// Settings' hardware pages (docs/SETTINGS.md): displays, Night Shift, battery, Bluetooth, idle, clock
export {
  displays, useDisplays, applyDisplay, keepDisplay, revertDisplay, arrangeDisplay, setDisplayMirror, setMainDisplay, setCaptureAllDisplays, displayConfirm, DisplayGlyph, nightPrefs, NIGHT_DEFAULTS, nightWindow,
  nightActive, nightAmount, setNightManual, previewNight, nightTick, sunTimes, battery, useBattery, BatteryGlyph, powerPrefs, POWER_DEFAULTS,
  powerProfiles, usePowerProfiles, onPowerProfileResult, bluetooth, useBluetooth, onBluetoothResult, timedate, useTimedate, menuClock, clockPrefs,
  CLOCK_DEFAULTS, applyIdle,
} from './hardware.jsx';
import { DisplayConfirm, BluetoothPrompt, startHardware } from './hardware.jsx';
export { DEFAULT_CATEGORIES, defaultFor, setDefaultFor, candidatesFor, defaultOpenerId, nativeOf } from './defaults.js';
export { notifyPrefs, appNotifyPrefs, setAppNotifyPrefs, dndActive, NOTIFY_DEFAULTS } from './notify.js';

// ---- installed themes ----

export const themes = {
  list: () => lord.themes.list().map((t) => {
    let meta = {};
    try { meta = t.meta ? JSON.parse(t.meta) : {}; } catch (e) { /* invalid theme.json */ }
    // translated name and description (shell.theme.<id>.*) or those from theme.json
    return { id: t.id, current: t.current, hidden: !!meta.hidden, name: tOptional(`shell.theme.${t.id}.name`) || meta.name || t.id,
      description: tOptional(`shell.theme.${t.id}.description`) || meta.description || '' };
  }).filter((t) => !t.hidden),         // the login screen (themes/greeter) can't be chosen
  current: () => lord.themes.list().find((t) => t.current)?.id || null,
  // Switches theme live: the server discards this JS context and loads the new one.
  use(id) {
    settings.set({ theme: id });
    lord.settings.save(JSON.stringify(settings.get(), null, 2));
    return lord.themes.use(id);
  },
};

// Session: log out, lock the screen or go back to the login screen so another
// user can sign in without closing this one (docs/SESSIONS.md). Locking and switching user need
// lord-sessiond (managed()); in a session viewed only from the browser, locking closes the
// browser's access.
export const session = {
  quit: () => { clipboardApi.beforeLogout(); lord.quit(); },
  managed: () => typeof lord.session !== 'undefined' && lord.session.managed(),
  lock: () => typeof lord.session !== 'undefined' && lord.session.lock(),
  switchUser: () => typeof lord.session !== 'undefined' && lord.session.switchUser(),
};

// ❖L or ❖Ctrl+Q lock the screen (as on macOS).
addListener('keydown', (e) => {
  const k = String(e.base || e.key || '').toLowerCase();
  if (e.meta && !e.alt && !e.repeat && ((k === 'l' && !e.ctrl && !e.shift) || (k === 'q' && e.ctrl)) && session.managed()) session.lock();
});

// The screen size changed: everything is redrawn with the new measurements.
const screenRev = createStore(0);
onScreenChange(() => screenRev.set((v) => v + 1));

// Entry point of a theme: respects the theme chosen by the user.
export function startShell(element) {
  const wanted = settings.get().theme;
  const current = themes.current();
  if (wanted && current && wanted !== current && lord.themes.use(wanted)) return;
  refreshPackages();                    // after the theme's configureWindows
  render(createElement(LocaleRoot, null, element));
  // live ISO: the installation assistant opens by itself, in front
  if (isLiveSystemOf() && getAppOf('installer')) setTimeout(() => wmOf.open('installer'), 300);
  startAlarms();                        // Clock alarms and timer, with the app closed
  startMail();                          // new-mail notifications and passwords on demand (docs/MAIL.md)
  startReminders();                     // calendar and reminder alarms, with the apps closed
  maybeStartTour();                     // "Welcome to LordOS", the first time
  startHardware();                      // Night Shift, idle timers, battery in the menu bar (hardware.jsx)
}

// Changing the language remounts the whole interface (windows, tabs and
// sessions live in stores and are not lost).
function LocaleRoot({ children }) {
  const { lang, region } = useStore(locale);
  const rev = useStore(screenRev);
  return createElement(Fragment, { key: `${lang}|${region}` }, rev ? cloneElement(children, { screenRev: rev }) : children, createElement(ClipboardPanel), createElement(SpotlightPanel), createElement(ClipboardToast), createElement(RemoteBadge), createElement(ImeOverlay), createElement(FloatingWindows), createElement(PermissionPrompt), createElement(EmojiPanel), createElement(PrintDialog), createElement(Powerbox), createElement(NotificationBanner), createElement(KeyringPrompt), createElement(BluetoothPrompt), createElement(DisplayConfirm));
}
