package main

//Compile in MSYS2 MINGW64
//Do: 
//go build -ldflags="-s -w -H=windowsgui" -o LocalChatHost.exe ./cmd/host/

//If go is installed on computer but not associated with MSYS@ MING64
//may have to do: export PATH=$PATH:/c/Program\ Files/Go/bin
//Change this according to the path of where go.exe is

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"localchat/server"
)

func main() {
	softCap := flag.Int64("cap", 200, "Soft capacity to trigger server shutdown")
	hcapPadding := flag.Int64("hcap", 50, "Extra bytes past soft cap before hard reject")
	port := flag.Int("port", 9000, "TCP port to listen on")
	pin := flag.String("pin", "", "Optional 4-digit PIN for access control")
	flag.Parse()

	srv, err := server.Start(server.Config{
		SoftCap:     *softCap,
		HardPadding: *hcapPadding,
		Port:        *port,
		PIN:         *pin,
	})
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		os.Exit(1)
	}

	// Wait for either Ctrl+C or the server hitting the soft cap
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	select {
	case <-sigCh:
		fmt.Println("\nReceived interrupt, shutting down...")
		srv.Stop()
	case <-srv.Done:
		fmt.Println("Server shut down (cap reached).")
	}
}