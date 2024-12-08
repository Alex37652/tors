package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"math/rand"
	"net/http"
	"strconv"
	"sync"
	"time"
)

var (
	serverPorts = []int{17423, 17424, 17425}
	portGen     = cyclePorts(serverPorts)

	keyLen      = 1
	valueLen    = 3
	reqTries    = 2 * len(serverPorts)
	reqNumber   = 30
	timeout     = 3 * time.Second
	checkDict   = make(map[string]string)
	checkDictMu sync.Mutex
)

type RequestData struct {
	Key   string `json:"Key"`
	Value string `json:"Value,omitempty"`
	Op    string `json:"Op"`
}

func cyclePorts(ports []int) chan int {
	ch := make(chan int)
	go func() {
		for {
			for _, port := range ports {
				ch <- port
			}
		}
	}()
	return ch
}

func genKeyStr() string {
	result := strconv.Itoa(rand.Intn(10))
	return result
}

func genValue(length int) string {
	letters := "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
	result := make([]byte, length)
	for i := range result {
		result[i] = letters[rand.Intn(len(letters))]
	}
	return string(result)
}

func genKeyValue(op string) RequestData {
	return RequestData{
		Key:   genKeyStr(),
		Value: genValue(valueLen),
		Op:    op,
	}
}

func genKey(op string) RequestData {
	return RequestData{
		Key: genKeyStr(),
		Op:  op,
	}
}

func generateRequestData() RequestData {
	ops := []string{"create", "read", "update", "delete"}
	op := ops[rand.Intn(len(ops))]
	if op == "create" || op == "update" {
		return genKeyValue(op)
	}
	return genKey(op)
}

func sendData(data RequestData, port int) (*http.Response, error) {
	url := fmt.Sprintf("http://localhost:%d/apply", port)
	jsonData, err := json.Marshal(data)
	if err != nil {
		return nil, err
	}

	client := http.Client{Timeout: timeout}
	if data.Op == "read" {
		req, err := http.NewRequest(http.MethodGet, url, bytes.NewBuffer(jsonData))
		if err != nil {
			return nil, err
		}
		req.Header.Set("Content-Type", "application/json")
		return client.Do(req)
	}

	resp, err := client.Post(url, "application/json", bytes.NewBuffer(jsonData))
	return resp, err
}

func applyOperation(data RequestData) {
	checkDictMu.Lock()
	defer checkDictMu.Unlock()

	switch data.Op {
	case "create", "update":
		checkDict[data.Key] = data.Value
	case "delete":
		delete(checkDict, data.Key)
	}
}

func sendRandomRequest() {
	data := generateRequestData()
	applyOperation(data)

	for i := 0; i < reqTries; i++ {
		time.Sleep(1 * time.Second)
		port := <-portGen
		fmt.Println("Trying port:", port)

		resp, err := sendData(data, port)
		if err != nil {
			fmt.Println("Timeout or error:", err)
			continue
		}
		defer resp.Body.Close()

		fmt.Println("Response status:", resp.StatusCode)
		if resp.StatusCode >= 200 && resp.StatusCode < 300 {
			return
		}
		if resp.StatusCode == 404 && data.Op == "read" {
			return
		}
	}

	panic("Server is not responding or fails")
}

func main() {
	rand.Seed(time.Now().UnixNano())

	for i := 0; i < reqNumber; i++ {
		sendRandomRequest()
	}

	checkDictMu.Lock()
	defer checkDictMu.Unlock()

	fmt.Println("Final state of checkDict:", checkDict)
}
