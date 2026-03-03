package server

import (
	"bufio"
	"context"
	"errors"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"net"
	"os"
	"strings"
	"sync"
	"time"
)

// Config (settings for the server entered by a user's input on the UI)
type Config struct {
	SoftCap     int64
	HardPadding int64
	LogPath     string // Defaults to "chatlog.txt" if empty
	Port        int    // TCP port, defaults to 9000 if 0
	PIN         string // Optional 4-digit PIN, empty = no PIN required
}

// Stats is a read-only snapshot of the server's current state.
type Stats struct {
	CurrentBytes   int64
	SoftMaxBytes   int64
	HardMaxBytes   int64
	ConnectedUsers int
	LastActivity   time.Time
	Online         bool
}

// Server is the main handle returned by Start().
type Server struct {
	mu           sync.Mutex
	file         *os.File
	softMaxBytes int64
	hardMaxBytes int64
	currentBytes int64

	clientsMu  sync.Mutex
	clients    map[net.Conn]int
	users      map[int]string
	nextUserID int

	listener     net.Listener
	ctx          context.Context
	cancel       context.CancelFunc
	lastActivity time.Time
	logPath      string
	port         int
	pin          string
	online       bool

	// OnLog is an optional callback. If set, all server log messages are sent here.
	// If nil, logs go to stdout.
	OnLog func(string)

	// Done is closed when the server fully shuts down.
	// Listen on this channel to detect cap-reached auto-shutdown.
	Done chan struct{}
}

// log sends a formatted message to the OnLog callback or stdout.
func (s *Server) log(format string, args ...interface{}) {
	msg := fmt.Sprintf(format, args...)
	if s.OnLog != nil {
		s.OnLog(msg)
	} else {
		fmt.Print(msg)
	}
}

// Stats returns a thread-safe read-only snapshot of the server state.
func (s *Server) Stats() Stats {
	s.mu.Lock()
	cur := s.currentBytes
	soft := s.softMaxBytes
	hard := s.hardMaxBytes
	last := s.lastActivity
	online := s.online
	s.mu.Unlock()

	s.clientsMu.Lock()
	userCount := len(s.clients)
	s.clientsMu.Unlock()

	return Stats{
		CurrentBytes:   cur,
		SoftMaxBytes:   soft,
		HardMaxBytes:   hard,
		ConnectedUsers: userCount,
		LastActivity:   last,
		Online:         online,
	}
}

func (s *Server) Port() int {
	return s.port
}

func (s *Server) HasPIN() bool {
	return s.pin != ""
}

// Start validates the config, opens the log file, binds the TCP listener,
// and launches the accept loop + UDP broadcaster in background goroutines.
func Start(cfg Config) (*Server, error) {
	if cfg.SoftCap <= 0 {
		return nil, errors.New("soft cap must be > 0")
	}
	if cfg.HardPadding < 0 {
		return nil, errors.New("hard padding must be >= 0")
	}
	if cfg.LogPath == "" {
		cfg.LogPath = fmt.Sprintf("%d.txt", cfg.Port)
	}

	hardCap := cfg.SoftCap + cfg.HardPadding

	file, err := os.OpenFile(cfg.LogPath, os.O_CREATE|os.O_TRUNC|os.O_RDWR, 0666)
	if err != nil {
		return nil, fmt.Errorf("error creating chat log: %w", err)
	}

	if cfg.Port == 0 {
		cfg.Port = 9000
	}

	listener, err := net.Listen("tcp", fmt.Sprintf(":%d", cfg.Port))

	if err != nil {
		file.Close()
		return nil, fmt.Errorf("error starting TCP server: %w", err)
	}

	ctx, cancel := context.WithCancel(context.Background())

	srv := &Server{
		file:         file,
		softMaxBytes: cfg.SoftCap,
		hardMaxBytes: hardCap,
		currentBytes: 0,
		clients:      make(map[net.Conn]int),
		users:        make(map[int]string),
		nextUserID:   1,
		listener:     listener,
		ctx:          ctx,
		cancel:       cancel,
		logPath:      cfg.LogPath,
		port:         cfg.Port,
		pin:          cfg.PIN,
		online:       true,
		Done:         make(chan struct{}),
	}

	go srv.acceptLoop()
	go srv.broadcastPresence()

	if srv.pin != "" {
		srv.log("Chat server running on TCP :%d (PIN protected)\n", srv.port)
	} else {
		srv.log("Chat server running on TCP :%d (Open)\n", srv.port)
	}
	srv.log("Soft Cap: %d, Hard Cap: %d\n\n", srv.softMaxBytes, srv.hardMaxBytes)

	return srv, nil
}

// Stop shuts down the server: cancels context, closes all connections,
// syncs and closes the log file, then signals Done.
func (s *Server) Stop() {
	s.mu.Lock()
	if !s.online {
		s.mu.Unlock()
		return // Already stopped
	}
	s.online = false
	s.mu.Unlock()

	s.cancel()
	s.listener.Close()

	// Disconnect all clients
	s.clientsMu.Lock()
	for conn := range s.clients {
		conn.Close()
	}
	s.clientsMu.Unlock()

	// Flush and close the log file
	s.mu.Lock()
	if s.file != nil {
		s.file.Sync()
		s.file.Close()
		s.file = nil
	}
	s.mu.Unlock()

	s.log("Server stopped.\n")
	close(s.Done)
}

// acceptLoop runs in a goroutine, accepting new TCP connections
// until the context is cancelled.
func (s *Server) acceptLoop() {
	for {
		conn, err := s.listener.Accept()
		if err != nil {
			// If context was cancelled, exit cleanly
			select {
			case <-s.ctx.Done():
				return
			default:
				s.log("Failed to accept connection: %v\n", err)
				continue
			}
		}
		go s.handleConnection(conn)
	}
}

// writeMessage appends a message to the log file and broadcasts it.
// If the soft cap is reached, it triggers a graceful shutdown.
func (s *Server) writeMessage(userID int, message string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	cleanMsg := strings.TrimSpace(message)
	fullMessage := fmt.Sprintf("%d: %s\n", userID, cleanMsg)
	msgBytes := int64(len(fullMessage))

	if s.currentBytes+msgBytes > s.hardMaxBytes {
		s.log("[X] Rejected: Would exceed hard cap (%d/%d bytes)\n",
			s.currentBytes+msgBytes, s.hardMaxBytes)
		return errors.New("message exceeds hard cap")
	}

	bytesWritten, err := s.file.WriteString(fullMessage)
	if err != nil {
		return err
	}

	s.currentBytes += int64(bytesWritten)
	s.lastActivity = time.Now()
	s.log("[✔] %d/%d bytes used. Hard Cap: %d. Msg: %s",
		s.currentBytes, s.softMaxBytes, s.hardMaxBytes, fullMessage)

	syncPacket := fmt.Sprintf("MSG|%d|%s", s.currentBytes, fullMessage)

	s.clientsMu.Lock()
	for conn := range s.clients {
		conn.Write([]byte(syncPacket))
	}
	s.clientsMu.Unlock()

	if s.currentBytes >= s.softMaxBytes {
		s.log("\n[!] Soft byte cap reached! Saving and shutting down...\n")
		go s.Stop() // Trigger in goroutine to avoid deadlock (we hold mu)
	}

	return nil
}

// editLastMessage finds the last message by the user and replaces it.
func (s *Server) editLastMessage(userID int, newText string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Read the current file content
	content, err := os.ReadFile(s.logPath)
	if err != nil {
		return err
	}

	// Split into individual message lines
	lines := strings.Split(string(content), "\n")
	prefix := fmt.Sprintf("%d: ", userID)

	foundIndex := -1
	// Iterate backwards to find the user's most recent message
	for i := len(lines) - 1; i >= 0; i-- {
		if strings.HasPrefix(lines[i], prefix) {
			foundIndex = i
			break
		}
	}

	if foundIndex == -1 {
		return errors.New("no previous message found")
	}

	// Replace the line (newText keeps any \x0B chars from Shift+Enter intact)
	cleanMsg := strings.TrimSpace(newText)
	lines[foundIndex] = fmt.Sprintf("%d: %s", userID, cleanMsg)

	// Re-join the file
	newContent := strings.Join(lines, "\n")
	msgBytes := int64(len(newContent))

	if msgBytes > s.hardMaxBytes {
		s.log("[X] Edit Rejected: Would exceed hard cap (%d/%d bytes)\n", msgBytes, s.hardMaxBytes)
		return errors.New("edit exceeds hard cap")
	}

	// Overwrite the file from scratch
	if err := s.file.Truncate(0); err != nil {
		return err
	}
	if _, err := s.file.Seek(0, 0); err != nil {
		return err
	}
	if _, err := s.file.WriteString(newContent); err != nil {
		return err
	}

	s.currentBytes = msgBytes
	s.lastActivity = time.Now()
	s.log("[✔] Edit applied. %d/%d bytes used.\n", s.currentBytes, s.softMaxBytes)

	// Broadcast the RELOAD packet followed by the raw file content.
	// Because the file content contains \n between messages, the C client will 
	// naturally process it line-by-line just like normal messages
	reloadPacket := fmt.Sprintf("RELOAD|%d\n%s", s.currentBytes, newContent)

	s.clientsMu.Lock()
	for conn := range s.clients {
		conn.Write([]byte(reloadPacket))
	}
	s.clientsMu.Unlock()

	// Check if this edit pushed us over the soft cap
	if s.currentBytes >= s.softMaxBytes {
		s.log("\n[!] Soft byte cap reached via edit! Saving and shutting down...\n")
		go s.Stop() 
	}

	return nil
}

// handleConnection manages a single client's lifecycle:
// handshake, sync, read loop, and cleanup on disconnect.
func (s *Server) handleConnection(conn net.Conn) {
	reader := bufio.NewReader(conn)

	// --- Access Gate Handshake ---
	authMsg, err := reader.ReadString('\n')
	if err != nil || strings.TrimSpace(authMsg) != "HELLO_LOCALCHAT_V1" {
		s.log("Blocked unauthorized connection from %s\n", conn.RemoteAddr().String())
		conn.Close()
		return
	}

	// --- PIN Challenge-Response ---
	if s.pin != "" {
		// Generate a random 16-byte nonce
		nonceBytes := make([]byte, 16)
		if _, err := rand.Read(nonceBytes); err != nil {
			s.log("Failed to generate nonce: %v\n", err)
			conn.Close()
			return
		}
		nonce := hex.EncodeToString(nonceBytes)

		// Send challenge to client
		conn.Write([]byte(fmt.Sprintf("AUTH_REQUIRED|%s\n", nonce)))

		// Wait for client's hashed response
		response, err := reader.ReadString('\n')
		if err != nil {
			conn.Close()
			return
		}

		response = strings.TrimSpace(response)
		if !strings.HasPrefix(response, "AUTH|") {
			conn.Write([]byte("AUTH_FAIL\n"))
			conn.Close()
			return
		}

		clientHash := strings.TrimPrefix(response, "AUTH|")

		// Compute expected: SHA256(nonce + pin) as hex
		expected := sha256.Sum256([]byte(nonce + s.pin))
		expectedHex := hex.EncodeToString(expected[:])

		if clientHash != expectedHex {
			conn.Write([]byte("AUTH_FAIL\n"))
			s.log("PIN auth failed from %s\n", conn.RemoteAddr().String())
			conn.Close()
			return
		}

		// Success
		conn.Write([]byte("AUTH_OK\n"))
		s.log("PIN auth passed from %s\n", conn.RemoteAddr().String())
	} else {
		// For no PIN required instances
		conn.Write([]byte("AUTH_NONE\n"))
	}

	// --- Register User ---
	s.clientsMu.Lock()
	userID := s.nextUserID
	s.nextUserID++
	s.clients[conn] = userID
	s.users[userID] = fmt.Sprintf("User%d", userID)

	var uparts []string
	for id, name := range s.users {
		uparts = append(uparts, fmt.Sprintf("%d:%s", id, name))
	}
	usersStr := strings.Join(uparts, "|")
	s.clientsMu.Unlock()

	// --- Send Initial Sync ---
	s.mu.Lock()
	history, _ := os.ReadFile(s.logPath)
	syncHeader := fmt.Sprintf("SYNC|%d|%d|%d|%d|%s\n",
		s.currentBytes, s.softMaxBytes, s.hardMaxBytes, len(history), usersStr)
	s.mu.Unlock()

	conn.Write([]byte(syncHeader))
	if len(history) > 0 {
		conn.Write(history)
	}

	s.broadcastUsers()

	// --- Cleanup on Disconnect ---
	defer func() {
		s.clientsMu.Lock()
		delete(s.clients, conn)
		delete(s.users, userID)
		s.clientsMu.Unlock()
		s.broadcastUsers()
		conn.Close()
		s.log("User %d disconnected\n", userID)
	}()

	s.log("User %d connected successfully: %s\n", userID, conn.RemoteAddr().String())

	// --- Read Loop ---
	for {
		msg, err := reader.ReadString('\n')
		if err != nil {
			return // Client disconnected or server shutting down
		}

		cleanMsg := strings.TrimSpace(msg)
		if cleanMsg == "" {
			continue
		}

		// Intercept /name command
		if strings.HasPrefix(cleanMsg, "/name ") {
			newName := strings.TrimSpace(strings.TrimPrefix(cleanMsg, "/name "))
			if newName != "" {
				s.clientsMu.Lock()
				s.users[userID] = newName
				s.clientsMu.Unlock()
				s.broadcastUsers()
			}
			continue
		}

		// Intercept /edit command
		if strings.HasPrefix(cleanMsg, "/edit ") {
			newText := strings.TrimPrefix(cleanMsg, "/edit ")
			err := s.editLastMessage(userID, newText)
			if err != nil {
				s.log("Edit failed for user %d: %v\n", userID, err)
			}
			continue
		}

		s.writeMessage(userID, msg)
	}
}

// broadcastPresence sends a UDP beacon every 2 seconds so clients
// can discover this server on the local network.
func (s *Server) broadcastPresence() {
	udpPort := s.port + 1
	addr, err := net.ResolveUDPAddr("udp4", fmt.Sprintf("255.255.255.255:%d", udpPort))
	if err != nil {
		s.log("UDP resolve error: %v\n", err)
		return
	}

	conn, err := net.DialUDP("udp4", nil, addr)
	if err != nil {
		s.log("UDP dial error: %v\n", err)
		return
	}
	defer conn.Close()

	for {
		select {
		case <-s.ctx.Done():
			return
		default:
		}

		s.mu.Lock()
		current := s.currentBytes
		soft := s.softMaxBytes
		hard := s.hardMaxBytes
		s.mu.Unlock()

		pinFlag := 0
		if s.pin != "" {
			pinFlag = 1
		}

		msg := fmt.Sprintf("LOCALCHAT|%d|%d|%d|%d|%d", s.port, current, soft, hard, pinFlag)
		conn.Write([]byte(msg))

		select {
		case <-s.ctx.Done():
			return
		case <-time.After(2 * time.Second):
		}
	}
}

// broadcastUsers sends the current user list to all connected clients.
func (s *Server) broadcastUsers() {
	s.clientsMu.Lock()
	defer s.clientsMu.Unlock()
	var parts []string
	for id, name := range s.users {
		parts = append(parts, fmt.Sprintf("%d:%s", id, name))
	}
	packet := "USERS|" + strings.Join(parts, "|") + "\n"
	for conn := range s.clients {
		conn.Write([]byte(packet))
	}
}