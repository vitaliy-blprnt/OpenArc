# Arc desktop interaction research

Research date: 2026-10-03 (America/Cayman). Scope: first-party desktop documentation and release notes. This is documentary research; it does not claim that an installed Arc build was operated or that every historical behavior survives unchanged. Product proposals below are explicitly separated from Arc facts.

## Status and reference scope

The Browser Company's May 26, 2025 letter says active development of Arc's core experience stopped while Chromium updates, security fixes, and related maintenance continued. It also describes Arc's proprietary Arc Development Kit as shared infrastructure with Dia, explaining why Arc itself had not been made open source. Treat Arc as a design reference rather than an available codebase to fork. [Letter to Arc members](https://browsercompany.substack.com/p/letter-to-arc-members-2025)

The current homepage states that Arc receives Chromium updates only. Therefore, “feature development has stopped” is better supported than “no longer maintained.” No installed binary or update process was verified. [Arc homepage](https://arc.net/)

## Visual observations

A collaborating researcher visually inspected Arc's official Space-switching screenshot. It shows a slim left rail occupying roughly one-fifth of the pictured window: compact address/navigation controls, square Favorites, the Space title and folder list, a divider, then New Tab and the open-tab list. Space dots and a plus sit at the bottom. The tinted sidebar continues as a thin frame around the dominant web-content pane. These are approximate observations of marketing imagery, not measured implementation specifications. [Official screenshot](https://arc.net/_next/image?q=100&url=%2Fspace-swiping.png&w=3840)

## The sidebar model

| Layer | Arc's documented behavior | Meaning for this browser |
| --- | --- | --- |
| Favorites, above the Space title | Compact icons at the top; up to 12; available across Spaces. The separate Profiles documentation identifies Favorites as profile data, so “global” should be read within the assigned Profile. [Favorites](https://resources.arc.net/hc/en-us/articles/19230755904151-Favorites-Top-Tabs-Across-Every-Space), [Profiles](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing) | Optional compact row for everyday destinations. Do not conflate it with per-Space bookmarks. |
| Pinned tabs and folders, above the divider | Saved to one Space, can be grouped in folders, and are excluded from automatic archive. Pin/unpin by dragging across the divider or using Cmd/Ctrl+D. [Pinned tabs](https://resources.arc.net/hc/en-us/articles/19231060187159-Pinned-Tabs-Tabs-you-want-to-stick-around) | The requested upper bookmarks section maps to persistent saved destinations. |
| Unpinned / Today tabs, below the divider | Idle unpinned tabs are eligible for automatic archive; viewing or clicking resets their timer. The documented default is 12 hours, timing is configurable by Profile, and Arc does not offer a complete off switch. [Auto Archive](https://resources.arc.net/hc/en-us/articles/19228855311127-Auto-Archive-Clean-as-you-go) | The requested lower open-tabs section should have an explicit retention policy. Arc's automatic cleanup is optional inspiration, not a user requirement. |

The important distinction is persistence and intent, not simply upper versus lower position. Arc calls a pin a hybrid of app and bookmark: it keeps a saved destination while permitting navigation. A pinned original URL can be restored, and a separate action replaces or edits that saved URL. [Pinned tabs](https://resources.arc.net/hc/en-us/articles/19231060187159-Pinned-Tabs-Tabs-you-want-to-stick-around), [Pinned URL FAQ](https://resources.arc.net/hc/en-us/articles/25541939922199-Why-Are-My-Pinned-Tabs-and-Favorites-Reverting-Back-to-Their-Original-URL-in-Arc-for-Desktop)

The Pinned article says clicking its icon resets a pin. Historical Windows notes mention double-clicking a Favorite and later a favicon action for returning to a pin's URL. Avoid copying a precise reset gesture without testing the target platform. The custom product can make “Return to saved URL” explicit. [Windows release notes](https://resources.arc.net/hc/en-us/articles/22513842649623-Arc-for-Windows-2023-2026-Release-Notes)

## Spaces versus Profiles

A Space owns its pinned section, unpinned section, icon, and theme. It represents a context such as Work, Personal, or a project. Switching is available through icons at the sidebar bottom, a horizontal trackpad swipe in the sidebar, numbered shortcuts, or the command bar. macOS uses Control plus a Space number; Windows uses Alt plus a Space number. [Spaces](https://resources.arc.net/hc/en-us/articles/19228064149143-Spaces-Distinct-Browsing-Areas)

A Profile is the account/session boundary: logins, autofill, cookies/cache, history, extensions, Favorites, archive policy, and browser settings. One Profile can serve multiple Spaces; creating a Space is therefore not equivalent to creating a separate login container. A new Profile begins without authentication/history. The documentation says Profiles themselves do not sync across devices even when associated Spaces and tabs do. [Profiles](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing)

Historical macOS release notes document restoring the last-used tab when switching into a Space. This behavior is a useful continuity principle for the custom design, with saved selection explicitly part of its own specification. [macOS 2023 release notes, August 10](https://resources.arc.net/hc/en-us/articles/20498377604887-Arc-for-macOS-2023-Release-Notes)

## Folders, visibility, and direct manipulation

Folders collect related saved tabs and are not automatically archived. They are created through the bottom plus control; tabs enter via drag and drop; contextual actions rename, move, or delete folders. On macOS, folder previews expose search/list access while the folder stays collapsed, and selecting a child can leave the active child visible under the closed folder. Sharing and preview capabilities are explicitly platform-scoped in the documentation. [Folders](https://resources.arc.net/hc/en-us/articles/19228419623447-Folders-Stash-Similar-Tabs-Together)

The whole sidebar can be toggled with Cmd+S on macOS or Ctrl+S on Windows. The Pinned section additionally has its own collapse control, labeled macOS-only in its guide. [Hide Sidebar](https://resources.arc.net/hc/en-us/articles/25619487530519-How-Do-You-Hide-the-Sidebar), [Pinned tabs](https://resources.arc.net/hc/en-us/articles/19231060187159-Pinned-Tabs-Tabs-you-want-to-stick-around)

Historical macOS documentation describes resizing by dragging the sidebar edge and resetting width with a double-click. Windows release notes document sidebar resizing and hover-triggered appearance of the undocked sidebar. Those are useful behavior references, but the research does not establish current exact width bounds or hover timing. [macOS 2022 release notes, February 16](https://resources.arc.net/hc/en-us/articles/20498417809815-Arc-for-macOS-2022-Release-Notes), [Windows release notes](https://resources.arc.net/hc/en-us/articles/22513842649623-Arc-for-Windows-2023-2026-Release-Notes)

## Command bar, Peek, and Split View

Arc's command bar combines navigation and actions. Cmd/Ctrl+T can find a Space and invoke actions such as pinning, creating a folder, toggling the sidebar, renaming, opening settings, and viewing history/archive. These ordinary command functions predate and do not depend on the later Arc Max AI features. [Spaces](https://resources.arc.net/hc/en-us/articles/19228064149143-Spaces-Distinct-Browsing-Areas), [Windows release notes, January 25, 2024](https://resources.arc.net/hc/en-us/articles/22513842649623-Arc-for-Windows-2023-2026-Release-Notes)

Peek is a temporary page over the current pinned destination. It supports dismissal, promotion to a normal tab, or promotion to Split View, reducing navigation away from a saved destination. The documentation describes automatic opening for links from pins/Favorites; its settings wording narrows this to links to other sites. Treat exact link matching as an unresolved implementation detail. [Peek](https://resources.arc.net/hc/en-us/articles/19335302900887-Peek-Preview-Sites-From-Pinned-Tabs)

Split View shows pages side by side or vertically and becomes a sidebar item that can be revisited. It can be created via shortcuts, commands specifying panel direction, or drag/drop on macOS; panels can be separated back into tabs. This is a composite workspace item, not only a transient layout toggle. [Split View](https://resources.arc.net/hc/en-us/articles/19335393146775-Split-View-View-Multiple-Tabs-at-Once)

## Archive, restore, and confidence

Archived unpinned tabs can be viewed and restored through the archive. Archive is therefore a recoverable state distinct from permanent deletion. [Auto Archive](https://resources.arc.net/hc/en-us/articles/19228855311127-Auto-Archive-Clean-as-you-go)

Arc also documents local sidebar-state backups on macOS accessible through Help > Restore Data, including multiple daily snapshots and progressively coarser older snapshots. This shows that persistent workspace recovery matters independently of individual recently closed tabs. [Restore Tabs](https://resources.arc.net/hc/en-us/articles/25625071960215-How-Do-I-Restore-My-Tabs-on-Arc-for-Desktop)

These sources do not fully specify all combinations of close button, Cmd/Ctrl+W, unloaded pin, active split, multiple windows, renderer crash, and relaunch. The custom browser needs its own explicit state transitions and acceptance cases. In particular, saved bookmark removal should be a different command from closing a live page; a visible saved destination should not imply a resident renderer process. The latter separation is consistent with historical Arc changes to lazily load Favorites, but the proposed semantics are our design choice. [macOS 2023 release notes, February 9](https://resources.arc.net/hc/en-us/articles/20498377604887-Arc-for-macOS-2023-Release-Notes)

## Documentation conflicts and implementation implications

- The Peek help article still says Windows cannot disable Peek. Windows release notes for June 20, 2024 explicitly add settings for both pinned-link Peek and Shift-click Peek. Treat the article's Windows limitation as outdated. [Peek](https://resources.arc.net/hc/en-us/articles/19335302900887-Peek-Preview-Sites-From-Pinned-Tabs), [Windows release notes](https://resources.arc.net/hc/en-us/articles/22513842649623-Arc-for-Windows-2023-2026-Release-Notes)
- macOS-only labels appear on pinned-section collapse, folder previews/sharing, and site-search documentation. Do not infer universal desktop parity. [Pinned tabs](https://resources.arc.net/hc/en-us/articles/19231060187159-Pinned-Tabs-Tabs-you-want-to-stick-around), [Folders](https://resources.arc.net/hc/en-us/articles/19228419623447-Folders-Stash-Similar-Tabs-Together), [Site Search](https://resources.arc.net/hc/en-us/articles/20855018192791-Site-Search-Directly-Search-any-Website)
- “Every Space” in the Favorites overview must be reconciled with Favorites belonging to Profiles. The safe model is profile-wide Favorites and space-specific saved items. [Favorites](https://resources.arc.net/hc/en-us/articles/19230755904151-Favorites-Top-Tabs-Across-Every-Space), [Profiles](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing)
- Historical release entries support interaction lineage, not pixel measurements or proof of current application behavior. Validate target shortcuts, pointer gestures, narrow windows, keyboard focus, and screen-reader structure in our own prototype.

## Proposed adaptation for this personal browser

Preserve the persistent left sidebar, readable vertical rows, upper saved destinations, lower open tabs, per-Space context, manual ordering, folders, and a keyboard command bar. Give Spaces distinct restrained colors/icons and restore their last selected page. Retain ordinary browser controls and a clear site identity/address surface.

The user confirmed Arc-style saved tabs that open in place. Store each saved destination separately from its live session state; selecting it focuses or opens its page at its permanent sidebar position. Closing the live page leaves the saved entry. Permit an explicit “Open another copy” action. Exact labels and close/reset details are proposed in the [browser plan](../BROWSER-PLAN.md), not claims of exact Arc parity.

Start with manual closing and recently closed recovery. Make any later auto-archive feature optional and reversible; do not inherit Arc's mandatory timer merely to imitate it. The browser plan includes separate login Profiles and optional shared Favorites in the core sequence; Split View, Peek, and richer folder previews are later choices. Exclude AI renaming, summarization, assistant search, and automatic AI grouping per the user's request.

Additional first-party visual references: the [Pinned-section screenshot](https://resources.arc.net/hc/article_attachments/20509285257111) and embedded demos in [Folders](https://resources.arc.net/hc/en-us/articles/19228419623447-Folders-Stash-Similar-Tabs-Together) and [Peek](https://resources.arc.net/hc/en-us/articles/19335302900887-Peek-Preview-Sites-From-Pinned-Tabs). These additional demos were not visually inspected.
