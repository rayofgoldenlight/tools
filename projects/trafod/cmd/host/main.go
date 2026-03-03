package main

import (
	"os"
	"fmt"
	"image/color"
	"strconv"
	"time"
	"net"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"

	"localchat/server"
)

// ============================================================
// THEME
// ============================================================

type chatTheme struct {
	dark bool
}

var (
	lightBg      = color.NRGBA{R: 240, G: 244, B: 248, A: 255}
	lightInputBg = color.NRGBA{R: 255, G: 255, B: 255, A: 255}
	lightFg      = color.NRGBA{R: 20, G: 20, B: 20, A: 255}
	lightAccent  = color.NRGBA{R: 13, G: 110, B: 253, A: 255}

	darkBg      = color.NRGBA{R: 18, G: 18, B: 18, A: 255}
	darkInputBg = color.NRGBA{R: 28, G: 28, B: 28, A: 255}
	darkFg      = color.NRGBA{R: 240, G: 240, B: 240, A: 255}
	darkAccent  = color.NRGBA{R: 220, G: 53, B: 69, A: 255}
)

func (t *chatTheme) Color(name fyne.ThemeColorName, variant fyne.ThemeVariant) color.Color {
	if t.dark {
		switch name {
		case theme.ColorNameBackground:
			return darkBg
		case theme.ColorNameOverlayBackground:
			return darkBg
		case theme.ColorNameInputBackground:
			return darkInputBg
		case theme.ColorNameButton:
			return darkAccent
		case theme.ColorNamePrimary:
			return darkAccent
		case theme.ColorNameForeground:
			return darkFg
		case theme.ColorNamePlaceHolder:
			return color.NRGBA{R: 120, G: 120, B: 120, A: 255}
		case theme.ColorNameSeparator:
			return darkAccent
		case theme.ColorNameHover:
			return color.NRGBA{R: 180, G: 40, B: 50, A: 255}
		case theme.ColorNameDisabled:
			return color.NRGBA{R: 200, G: 200, B: 200, A: 255}
		case theme.ColorNameDisabledButton:
			return color.NRGBA{R: 50, G: 50, B: 50, A: 255}
		}
	} else {
		switch name {
		case theme.ColorNameBackground:
			return lightBg
		case theme.ColorNameOverlayBackground:
			return lightBg
		case theme.ColorNameInputBackground:
			return lightInputBg
		case theme.ColorNameButton:
			return lightAccent
		case theme.ColorNamePrimary:
			return lightAccent
		case theme.ColorNameForeground:
			return lightFg
		case theme.ColorNamePlaceHolder:
			return color.NRGBA{R: 140, G: 140, B: 140, A: 255}
		case theme.ColorNameSeparator:
			return lightAccent
		case theme.ColorNameHover:
			return color.NRGBA{R: 10, G: 90, B: 210, A: 255}
		case theme.ColorNameDisabled:
			return color.NRGBA{R: 160, G: 160, B: 160, A: 255}
		case theme.ColorNameDisabledButton:
			return color.NRGBA{R: 200, G: 200, B: 200, A: 255}
		}
	}
	return theme.DefaultTheme().Color(name, variant)
}

func (t *chatTheme) Font(style fyne.TextStyle) fyne.Resource {
	return theme.DefaultTheme().Font(style)
}

func (t *chatTheme) Icon(name fyne.ThemeIconName) fyne.Resource {
	return theme.DefaultTheme().Icon(name)
}

func (t *chatTheme) Size(name fyne.ThemeSizeName) float32 {
	switch name {
	case theme.SizeNamePadding:
		return 6
	case theme.SizeNameInlineIcon:
		return 20
	case theme.SizeNameText:
		return 14
	case theme.SizeNameHeadingText:
		return 22
	case theme.SizeNameSubHeadingText:
		return 16
	}
	return theme.DefaultTheme().Size(name)
}

func getLocalIP() string {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return "unknown"
	}
	for _, addr := range addrs {
		if ipnet, ok := addr.(*net.IPNet); ok && !ipnet.IP.IsLoopback() && ipnet.IP.To4() != nil {
			return ipnet.IP.String()
		}
	}
	return "127.0.0.1"
}

var appIconBytes, _ = os.ReadFile("icon2.ico")

// Light mode icon:
var lightIconRes = fyne.NewStaticResource("light.svg", []byte(`
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <rect x="3" y="3" width="26" height="26" rx="3" fill="none" stroke="#0D6EFD" stroke-width="2.5"/>
  <polygon points="16,7 25,16 16,25 7,16" fill="#0D6EFD"/>
</svg>`))

// Dark mode icon:
var darkIconRes = fyne.NewStaticResource("dark.svg", []byte(`
<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
  <polygon points="16,3 29,16 16,29 3,16" fill="none" stroke="#DC3545" stroke-width="2.5"/>
  <rect x="10" y="10" width="12" height="12" rx="2" fill="#DC3545"/>
</svg>`))

// TappableIcon wraps a canvas.Image so it can receive tap events.
type TappableIcon struct {
	widget.BaseWidget
	img      *canvas.Image
	OnTapped func()
}

func NewTappableIcon(img *canvas.Image, onTapped func()) *TappableIcon {
	t := &TappableIcon{img: img, OnTapped: onTapped}
	t.ExtendBaseWidget(t)
	return t
}

func (t *TappableIcon) CreateRenderer() fyne.WidgetRenderer {
	return widget.NewSimpleRenderer(t.img)
}

func (t *TappableIcon) Tapped(_ *fyne.PointEvent) {
	if t.OnTapped != nil {
		t.OnTapped()
	}
}

func (t *TappableIcon) TappedSecondary(_ *fyne.PointEvent) {}

func (t *TappableIcon) MinSize() fyne.Size {
	return t.img.MinSize()
}

func isDigitsOnly(s string) bool {
	for _, c := range s {
		if c < '0' || c > '9' {
			return false
		}
	}
	return true
}

// ============================================================
// MAIN
// ============================================================

func main() {
	currentTheme := &chatTheme{dark: false}
	a := app.New()
	if len(appIconBytes) > 0 {
		a.SetIcon(fyne.NewStaticResource("icon2.ico", appIconBytes))
	}
	a.Settings().SetTheme(currentTheme)

	w := a.NewWindow("LocalChat Host")
	w.Resize(fyne.NewSize(440, 400))

	// Track running server instance
	var srv *server.Server

	// --- Title ---
	title := canvas.NewText("LocalChat Host", lightAccent)
	title.TextSize = 24
	title.Alignment = fyne.TextAlignCenter
	title.TextStyle = fyne.TextStyle{Bold: true}

	// --- Accent Line ---
	accentLine := canvas.NewRectangle(lightAccent)
	accentLine.SetMinSize(fyne.NewSize(0, 2))

	// --- Theme Toggle Icon ---
	themeIcon := canvas.NewImageFromResource(lightIconRes)
	themeIcon.SetMinSize(fyne.NewSize(32, 32))
	themeIcon.FillMode = canvas.ImageFillContain
	themeTap := NewTappableIcon(themeIcon, nil)

	rightSpacer := canvas.NewRectangle(color.Transparent)
	rightSpacer.SetMinSize(fyne.NewSize(32, 32))

	// --- Top Bar ---
	topBar := container.NewBorder(nil, nil, themeTap, rightSpacer, title)

	// --- Error Label ---
	errorLabel := canvas.NewText("", darkAccent)
	errorLabel.TextSize = 12
	errorLabel.Alignment = fyne.TextAlignCenter

	// --- Input Fields ---
	softCapEntry := widget.NewEntry()
	softCapEntry.SetPlaceHolder("200")
	softCapEntry.SetText("200")

	hardPadEntry := widget.NewEntry()
	hardPadEntry.SetPlaceHolder("50")
	hardPadEntry.SetText("50")

	portEntry := widget.NewEntry()
	portEntry.SetPlaceHolder("9000")
	portEntry.SetText("9000")

	pinEntry := widget.NewEntry()
	pinEntry.SetPlaceHolder("Empty = No PIN")
	pinEntry.SetText("")

	pinEntry.OnChanged = func(s string) {
		filtered := ""
		for _, r := range s {
			// Keep only numeric digits
			if r >= '0' && r <= '9' {
				filtered += string(r)
			}
		}

		// Cap the length at 4 characters
		if len(filtered) > 4 {
			filtered = filtered[:4]
		}
		
		// If user typed a letter, or exceeded 4 characters, 
		// overwrite the text box with corrected string.
		if filtered != s {
			pinEntry.SetText(filtered)
		}
	}

	formGrid := container.New(layout.NewFormLayout(),
		widget.NewLabel("Soft Cap (bytes):"), softCapEntry,
		widget.NewLabel("Hard Padding (bytes):"), hardPadEntry,
		widget.NewLabel("TCP Port:"), portEntry,
		widget.NewLabel("PIN (4 digits):"), pinEntry,
	)

	// --- Live Stats Labels ---
	statusLabel := canvas.NewText("● Offline", darkAccent)
	statusLabel.TextSize = 16
	statusLabel.TextStyle = fyne.TextStyle{Bold: true}
	statusLabel.Alignment = fyne.TextAlignCenter

	bytesLabel := widget.NewLabel("Storage: —")
	bytesLabel.Alignment = fyne.TextAlignCenter

	usersLabel := widget.NewLabel("Connected Users: —")
	usersLabel.Alignment = fyne.TextAlignCenter

	activityLabel := widget.NewLabel("Last Activity: —")
	activityLabel.Alignment = fyne.TextAlignCenter

	networkLabel := canvas.NewText("Waiting to start...", lightFg)
	networkLabel.TextSize = 11
	networkLabel.Alignment = fyne.TextAlignCenter

	// Stats panel is hidden initially
	statsPanel := container.NewVBox(
		statusLabel,
		widget.NewSeparator(),
		bytesLabel,
		usersLabel,
		activityLabel,
		widget.NewSeparator(),
		networkLabel,
	)
	statsPanel.Hide()

	// --- Log Area ---
	logArea := widget.NewMultiLineEntry()
	logArea.SetPlaceHolder("Server log output...")
	logArea.Wrapping = fyne.TextWrapWord
	logArea.SetMinRowsVisible(4)
	logArea.Disable() // Read-only
	logArea.Hide()

	// --- Buttons ---
	var startBtn *widget.Button
	var stopBtn *widget.Button

	// --- Stats Ticker (updates UI 2x per second) ---
	statsTicker := func() {
		for {
			time.Sleep(500 * time.Millisecond)
			if srv == nil {
				return
			}
			stats := srv.Stats()
			if !stats.Online {
				// Server shut itself down (cap reached) or was stopped
				// If srv is already nil, the Stop button handled cleanup
				if srv == nil {
					return
				}
				srv = nil

				statusLabel.Text = "● Offline (Cap Reached)"
				statusLabel.Color = darkAccent
				statusLabel.Refresh()

				bytesLabel.SetText(fmt.Sprintf("Final Storage: %d / %d bytes",
					stats.CurrentBytes, stats.SoftMaxBytes))
				usersLabel.SetText("Connected Users: 0")
				activityLabel.SetText("Server stopped — soft cap reached")
				networkLabel.Text = "Waiting to start..."
				networkLabel.Refresh()

				softCapEntry.Enable()
				hardPadEntry.Enable()
				portEntry.Enable()
				pinEntry.Enable()
				startBtn.Enable()
				stopBtn.Disable()
				return
			}

			// Update live labels
			bytesLabel.SetText(fmt.Sprintf("Storage: %d / %d bytes (Hard: %d)",
				stats.CurrentBytes, stats.SoftMaxBytes, stats.HardMaxBytes))
			usersLabel.SetText(fmt.Sprintf("Connected Users: %d", stats.ConnectedUsers))

			if stats.LastActivity.IsZero() {
				activityLabel.SetText("Last Activity: No messages yet")
			} else {
				ago := time.Since(stats.LastActivity).Round(time.Second)
				activityLabel.SetText(fmt.Sprintf("Last Activity: %s ago", ago))
			}
		}
	}

	// --- Start Logic ---
	startServer := func() {
		errorLabel.Text = ""
		errorLabel.Refresh()

		softVal, err1 := strconv.ParseInt(softCapEntry.Text, 10, 64)
		hardVal, err2 := strconv.ParseInt(hardPadEntry.Text, 10, 64)

		if err1 != nil || softVal <= 0 {
			errorLabel.Text = "Soft Cap must be a number greater than 0"
			errorLabel.Refresh()
			return
		}
		if err2 != nil || hardVal < 0 {
			errorLabel.Text = "Hard Padding must be a number >= 0"
			errorLabel.Refresh()
			return
		}

		portVal := 9000
		if portEntry.Text != "" {
			p, errP := strconv.Atoi(portEntry.Text)
			if errP != nil || p < 1 || p > 65535 {
				errorLabel.Text = "Port must be a number between 1 and 65535"
				errorLabel.Refresh()
				return
			}
			portVal = p
		}

		pinVal := pinEntry.Text
		if pinVal != "" && (len(pinVal) != 4 || !isDigitsOnly(pinVal)) {
			errorLabel.Text = "PIN must be exactly 4 digits (or empty for no PIN)"
			errorLabel.Refresh()
			return
		}

		cfg := server.Config{
			SoftCap:     softVal,
			HardPadding: hardVal,
			Port:        portVal,
			PIN:         pinVal,
		}

		var err error
		srv, err = server.Start(cfg)
		if err != nil {
			errorLabel.Text = fmt.Sprintf("Failed to start: %v", err)
			errorLabel.Refresh()
			return
		}

		// Pipe server logs to the GUI log area
		srv.OnLog = func(msg string) {
			logArea.SetText(logArea.Text + msg)
			// Auto-scroll to bottom
			logArea.CursorRow = len(logArea.Text)
		}

		// Update UI state
		statusLabel.Text = "● Online"
		if currentTheme.dark {
			statusLabel.Color = color.NRGBA{R: 0, G: 200, B: 80, A: 255}
		} else {
			statusLabel.Color = color.NRGBA{R: 0, G: 160, B: 60, A: 255}
		}
		statusLabel.Refresh()

		softCapEntry.Disable()
		hardPadEntry.Disable()
		portEntry.Disable()
		pinEntry.Disable()
		startBtn.Disable()
		stopBtn.Enable()
		statsPanel.Show()
		logArea.Show()

		localIP := getLocalIP()
		pinStatus := "Open"
		if pinVal != "" {
			pinStatus = "PIN Protected"
		}
		networkLabel.Text = fmt.Sprintf("Hosting on %s  •  TCP :%d  •  UDP :%d  •  %s",
			localIP, portVal, portVal+1, pinStatus)
		networkLabel.Refresh()

		// Start the live stats updater
		go statsTicker()
	}

	startBtn = widget.NewButton("▶  Start Server", func() {
		startServer()
	})
	startBtn.Importance = widget.HighImportance

	stopBtn = widget.NewButton("■  Stop Server", func() {
		if srv == nil {
			return
		}
		srv.Stop()
		srv = nil

		statusLabel.Text = "● Offline (Stopped)"
		statusLabel.Color = darkAccent
		statusLabel.Refresh()

		bytesLabel.SetText("Storage: —")
		usersLabel.SetText("Connected Users: —")
		activityLabel.SetText("Server stopped by host")
		networkLabel.Text = "Waiting to start..."
		networkLabel.Refresh()

		softCapEntry.Enable()
		hardPadEntry.Enable()
		portEntry.Enable()
		pinEntry.Enable()
		startBtn.Enable()
		stopBtn.Disable()
		logArea.SetText("")
	})

	stopBtn.Importance = widget.DangerImportance
	stopBtn.Disable()

	// --- Enter Key triggers Start ---
	softCapEntry.OnSubmitted = func(_ string) { startServer() }
	hardPadEntry.OnSubmitted = func(_ string) { startServer() }
	portEntry.OnSubmitted = func(_ string) { startServer() }
	pinEntry.OnSubmitted = func(_ string) { startServer() }

	accentLine2 := canvas.NewRectangle(lightAccent)
	accentLine2.SetMinSize(fyne.NewSize(0, 2))

	// --- Shared Theme Toggle Logic ---
	toggleTheme := func() {
		currentTheme.dark = !currentTheme.dark
		a.Settings().SetTheme(currentTheme)

		if currentTheme.dark {
			themeIcon.Resource = darkIconRes
			themeIcon.Refresh()
			title.Color = darkAccent
			accentLine.FillColor = darkAccent
			networkLabel.Color = darkFg
			if srv != nil && srv.Stats().Online {
				statusLabel.Color = color.NRGBA{R: 0, G: 200, B: 80, A: 255}
			}
		} else {
			themeIcon.Resource = lightIconRes
			themeIcon.Refresh()
			title.Color = lightAccent
			accentLine.FillColor = lightAccent
			networkLabel.Color = lightFg
			if srv != nil && srv.Stats().Online {
				statusLabel.Color = color.NRGBA{R: 0, G: 160, B: 60, A: 255}
			}
		}
		title.Refresh()
		accentLine.Refresh()
		accentLine2.FillColor = accentLine.FillColor
		accentLine2.Refresh()
		networkLabel.Refresh()
		statusLabel.Refresh()
	}

	// Wire both the button and F2 to the same function
	themeTap.OnTapped = toggleTheme
	w.Canvas().SetOnTypedKey(func(ev *fyne.KeyEvent) {
		if ev.Name == fyne.KeyF2 {
			toggleTheme()
		}
	})

	// --- Buttons Row ---
	buttonsRow := container.NewGridWithColumns(2, startBtn, stopBtn)

	// --- Full Layout ---
	content := container.NewVBox(
		topBar,
		accentLine,
		widget.NewSeparator(),
		formGrid,
		errorLabel,
		buttonsRow,
		accentLine2,
		statsPanel,
		logArea,
	)

	w.SetContent(container.NewPadded(content))
	w.ShowAndRun()
}