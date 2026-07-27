import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import re
import csv
from datetime import datetime

class TeensySerialGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Teensy Serial Dashboard")

        # Serial communication variables
        self.serial_port = None
        self.is_reading = False
        self.csv_file = None
        self.csv_writer = None
        self.target_resistance_value = 0
        
        # Resistance state tracking - SAME ARRAY AS TEENSY
        #self.resistance_states = [8000, 9200, 11000, 13600, 17700, 25400, 45200, 200000] #For 3-Bit
        self.resistance_states = [8000, 8550, 9170, 9900, 10750, 11800, 13000, 14500, 16400, 18900, 22200, 27000, 34500, 47600, 76900, 200000] #For 4-Bit
        '''self.resistance_states = [8000, 8250, 8500, 8800, 9100, 9400, 9800, 10200,
                                  10600, 11100, 11600, 12100, 12700, 13400, 14100, 14900,
                                  15800, 16800, 18000, 19400, 21000, 22800, 25100, 27800,
                                  31200, 35400, 41000, 48800, 60200, 78500, 112700, 200000]'''  #For 5-Bit
        
        '''self.resistance_states = [8000, 8120, 8250, 8380, 8520, 8660, 8800, 8950,
                                   9110, 9270, 9430, 9610, 9790, 9970, 10170, 10370,
                                   10570, 10790, 11020, 11260, 11500, 11760, 12000, 12300,
                                   12600, 12930, 13250, 13600, 13900, 14300, 14700, 15100,
                                   15600, 16100, 16600, 17100, 17700, 18300, 19000, 19700, 
                                   20500, 21300, 22200, 23200, 24300, 25500, 26800, 28200,
                                   29800, 31580, 33600, 35900, 38500, 41600, 45100, 49400,
                                   54500, 60800, 68800, 79200, 93300, 113500, 144800, 200000]''' #For 6-Bit
        
        self.current_resistance_idx = 0
        self.file_created_for_idx = -1

        # UI Components
        self.create_widgets()

        # Regex for parsing serial data
        float_num = r"([+-]?\d+(?:\.\d+)?)"
        int_num   = r"([+-]?\d+)"

        self.data_pattern = re.compile(
            rf"Vsh1:\s*{float_num}mV\s*Ish1:\s*{float_num}uA\s*Vram1:\s*{float_num}mV\s*?RES1:\s*{float_num}\s*Ohms\s*"
            rf"Vsh2:\s*{float_num}mV\s*Ish2:\s*{float_num}uA\s*Vram2:\s*{float_num}mV\s*?RES2:\s*{float_num}\s*Ohms\s*"
            rf"Vsh3:\s*{float_num}mV\s*Ish3:\s*{float_num}uA\s*Vram3:\s*{float_num}mV\s*?RES3:\s*{float_num}\s*Ohms\s*"
            rf"Vsh4:\s*{float_num}mV\s*Ish4:\s*{float_num}uA\s*Vram4:\s*{float_num}mV\s*?RES4:\s*{float_num}\s*Ohms\s*"
            rf"Iter:\s*{int_num}\s*Steps\s*Pass:\s*{int_num}\s*Cycles\s*"
            rf"Mode:\s*{int_num}\s*"
            rf"U1Vbl1:\s*{float_num}mV\s*U1Vbl2:\s*{float_num}mV\s*U2Vbl1:\s*{float_num}mV\s*U2Vbl2:\s*{float_num}mV\s*"
            rf"U1Vwl1:\s*{float_num}mV\s*U2Vwl1:\s*{float_num}mV\s*U1Vsl1:\s*{float_num}mV\s*U2Vsl1:\s*{float_num}mV\s*"
            rf"LRS:\s*{float_num}\s*Ohms\s*HRS:\s*{float_num}\s*Ohms\s*OpState:\s*([A-Za-z0-9_-]+)"
        )

    def create_widgets(self):
        # COM Port Selection
        frame_top = ttk.Frame(self.root)
        frame_top.pack(pady=10)

        ttk.Label(frame_top, text="COM Port:").grid(row=0, column=0, padx=5)
        self.combobox_ports = ttk.Combobox(frame_top, width=10, state="readonly")
        self.combobox_ports.grid(row=0, column=1, padx=5)
        self.refresh_ports()

        ttk.Button(frame_top, text="Refresh", command=self.refresh_ports).grid(row=0, column=2, padx=5)
        
        # Target Resistance Display
        ttk.Label(frame_top, text="Target Res:").grid(row=0, column=3, padx=5)
        self.target_res_label = ttk.Label(frame_top, text="0 Ω", font=("Arial", 10, "bold"))
        self.target_res_label.grid(row=0, column=4, padx=5)
        
        # File Status Display
        ttk.Label(frame_top, text="File:").grid(row=0, column=5, padx=5)
        self.file_status_label = ttk.Label(frame_top, text="Waiting...", font=("Arial", 8))
        self.file_status_label.grid(row=0, column=6, padx=5)

        # ============ FIXED: Data Display - ONLY 4 CHANNELS ============
        frame_data = ttk.Frame(self.root)
        frame_data.pack(pady=10)

        self.dials = {}
        
        # CORRECT labels - only 4 channels with 4 measurements each = 16 total
        labels = [
            ("Vsh1 (mV)", "Ish1 (uA)", "Vram1 (mV)", "RES1 (Ohms)"),
            ("Vsh2 (mV)", "Ish2 (uA)", "Vram2 (mV)", "RES2 (Ohms)"),
            ("Vsh3 (mV)", "Ish3 (uA)", "Vram3 (mV)", "RES3 (Ohms)"),
            ("Vsh4 (mV)", "Ish4 (uA)", "Vram4 (mV)", "RES4 (Ohms)")
        ]

        for row, channel_labels in enumerate(labels):
            frame_row = ttk.Frame(frame_data)
            frame_row.pack(pady=2)
            
            # Channel header
            channel_frame = ttk.Frame(frame_row, relief="ridge", borderwidth=2)
            channel_frame.pack(side="left", padx=5, pady=2)
            ttk.Label(channel_frame, text=f"Channel {row+1}", font=("Arial", 10, "bold")).pack(padx=5, pady=2)
            
            # Measurements for this channel
            for label_text in channel_labels:
                dial_frame = ttk.Frame(frame_row, relief="ridge", borderwidth=2)
                dial_frame.pack(side="left", padx=5, pady=2)
                
                ttk.Label(dial_frame, text=label_text).pack(padx=5, pady=2)
                value_label = ttk.Label(dial_frame, text="0", font=("Arial", 12, "bold"))
                value_label.pack(padx=5, pady=2)
                self.dials[label_text] = value_label
        # ===============================================================

        # Status indicators frame
        frame_status = ttk.Frame(self.root, relief="ridge", borderwidth=2)
        frame_status.pack(padx=5, pady=5, fill="x")

        # Iter (cycles)
        iter_frame = ttk.Frame(frame_status)
        iter_frame.pack(side="left", padx=20, pady=5)
        ttk.Label(iter_frame, text="Iter (cycles)", font=("Arial", 9)).pack()
        self.iter_label = ttk.Label(iter_frame, text="0", font=("Arial", 12, "bold"))
        self.iter_label.pack()

        # Pass (cycles)
        pass_frame = ttk.Frame(frame_status)
        pass_frame.pack(side="left", padx=20, pady=5)
        ttk.Label(pass_frame, text="Pass (cycles)", font=("Arial", 9)).pack()
        self.pass_label = ttk.Label(pass_frame, text="0", font=("Arial", 12, "bold"))
        self.pass_label.pack()
        
        # MODE
        mode_frame = ttk.Frame(frame_status)
        mode_frame.pack(side="left", padx=20, pady=5)
        ttk.Label(mode_frame, text="MODE", font=("Arial", 9)).pack()
        self.mode_label = ttk.Label(mode_frame, text="0", font=("Arial", 12, "bold"))
        self.mode_label.pack()
        
        # Operation State
        opstate_frame = ttk.Frame(frame_status)
        opstate_frame.pack(side="left", padx=20, pady=5)
        ttk.Label(opstate_frame, text="Operation State", font=("Arial", 9)).pack()
        self.opstate_label = ttk.Label(opstate_frame, text="-", font=("Arial", 12, "bold"))
        self.opstate_label.pack()
        
        # Resistance values frame
        frame_res = ttk.Frame(self.root, relief="ridge", borderwidth=2)
        frame_res.pack(padx=5, pady=5, fill="x")
        
        # LRS
        lrs_frame = ttk.Frame(frame_res)
        lrs_frame.pack(side="left", padx=20, pady=5)
        ttk.Label(lrs_frame, text="LRS (Ohms)", font=("Arial", 9)).pack()
        self.current_resistanceLRS_label = ttk.Label(lrs_frame, text="0", font=("Arial", 12, "bold"))
        self.current_resistanceLRS_label.pack()
        
        # HRS
        hrs_frame = ttk.Frame(frame_res)
        hrs_frame.pack(side="left", padx=20, pady=5)
        ttk.Label(hrs_frame, text="HRS (Ohms)", font=("Arial", 9)).pack()
        self.current_resistanceHRS_label = ttk.Label(hrs_frame, text="0", font=("Arial", 12, "bold"))
        self.current_resistanceHRS_label.pack()

        # Mode Index
        frame_index = ttk.Frame(self.root)
        frame_index.pack(pady=5)
        self.index_label = ttk.Label(frame_index, text="1. SET | 2. RESET | 3. LOOP | 4. VERIFY", font=("Arial", 9))
        self.index_label.pack()

        # Control Buttons
        frame_buttons = ttk.Frame(self.root)
        frame_buttons.pack(pady=10)

        ttk.Button(frame_buttons, text="Read", command=self.start_reading).pack(side="left", padx=5)
        ttk.Button(frame_buttons, text="SET", command=lambda: self.send_command("SET")).pack(side="left", padx=5)
        ttk.Button(frame_buttons, text="RESET", command=lambda: self.send_command("CLEAR")).pack(side="left", padx=5)
        ttk.Button(frame_buttons, text="LOOP", command=lambda: self.send_command("LOOP")).pack(side="left", padx=5)
        ttk.Button(frame_buttons, text="VERIFY", command=lambda: self.send_command("VERIFY")).pack(side="left", padx=5)
        ttk.Button(frame_buttons, text="Stop", command=self.stop_reading).pack(side="left", padx=5)

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        self.combobox_ports["values"] = [port.device for port in ports]
        if ports:
            self.combobox_ports.current(0)

    def send_command(self, command):
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.write((command + "\n").encode('utf-8'))
            print(f"Command sent: {command}")
            
            # ============ SIMPLE FIX ============
            if command == "LOOP":
                print("⏳ Waiting 2 seconds for Teensy to start...")
                time.sleep(2)
                self.serial_port.reset_input_buffer()
                print("✅ Ready to receive data")
            # ====================================
        else:
            messagebox.showerror("Error", "No serial port connected.")

    def create_csv_file(self, resistance_value, timestamp):
        """Create a new CSV file with the given resistance value"""
        if resistance_value > 0:
            filename = f"C:/Users/SER7/Documents/GitHub/rram_tests/teensy/GUI_Python/ReRAM_{resistance_value}ohm_{timestamp}.csv"
        else:
            filename = f"C:/Users/SER7/Documents/GitHub/rram_tests/teensy/GUI_Python/ReRAM_Log_{timestamp}.csv"
        
        if self.csv_file:
            self.close_csv()
        
        self.csv_file = open(filename, mode="w", newline="")
        self.csv_writer = csv.writer(self.csv_file)
        
        headers = [
            "Timestamp",
            "Vsh1 (mV)", "Ish1 (uA)", "Vram1 (mV)", "RES1 (Ohms)",
            "Vsh2 (mV)", "Ish2 (uA)", "Vram2 (mV)", "RES2 (Ohms)",
            "Vsh3 (mV)", "Ish3 (uA)", "Vram3 (mV)", "RES3 (Ohms)",
            "Vsh4 (mV)", "Ish4 (uA)", "Vram4 (mV)", "RES4 (Ohms)",
            "Iter", "Pass", "Mode", "V-U1BL1", "V-U1BL2", "V-U2BL1",
            "V-U2BL2", "V-U1WL1", "V-U2WL1", "V-U1SL1", "V-U2SL1",
            "LRS (Ohms)", "HRS (Ohms)", "OperationState"
        ]
        self.csv_writer.writerow(headers)
        self.csv_file.flush()
        
        filename_short = filename.split('/')[-1]
        self.file_status_label.config(text=f"{filename_short[:20]}...")
        print(f"\n✅ FILE CREATED: {filename_short}")
        print(f"   Resistance: {resistance_value}Ω")
        return filename

    def start_reading(self):
        if not self.combobox_ports.get():
            messagebox.showerror("Error", "Please select a COM port.")
            return

        try:
            self.serial_port = serial.Serial(self.combobox_ports.get(), 2000000, timeout=1)
            self.is_reading = True
            self.start_time = time.time()
            
            # Reset tracking
            self.file_created_for_idx = -1
            self.target_resistance_value = 0
            self.target_res_label.config(text="0 Ω")
            self.file_status_label.config(text="Listening...")
            
            print("\n" + "="*60)
            print("STARTING DATA ACQUISITION")
            print("="*60 + "\n")
            
            threading.Thread(target=self.read_serial, daemon=True).start()
            
        except Exception as e:
            messagebox.showerror("Error", f"Failed to open serial port: {e}")

    def read_serial(self):
        buffer = ""
        
        while self.is_reading:
            try:
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting).decode('utf-8', errors='replace')
                    buffer += data
                    
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        
                        if not line:
                            continue
                        
                        # ============ PRINT SELECTIVE TEENSY OUTPUT ============
                        # Print ONLY lines that are NOT loggerOut data
                        if not line.startswith('Vsh1:'):
                            print(f" TEENSY: {line}")
                        # =======================================================
                        
                        # Capture Resistance Index
                        if "ResIdx:" in line:
                            try:
                                idx_str = line.split("ResIdx:")[1].strip()
                                idx = int(idx_str)
                                
                                if 0 <= idx < len(self.resistance_states):
                                    self.current_resistance_idx = idx
                                    self.target_resistance_value = self.resistance_states[idx]
                                    self.target_res_label.config(text=f"{self.target_resistance_value} Ω")
                                    
                                    time_elapsed = time.time() - self.start_time
                                    print(f"\n ResIdx: {idx} → {self.target_resistance_value}Ω (t={time_elapsed:.2f}s)")
                                    
                                    # ALWAYS create file for EVERY ResIdx
                                    timestamp = datetime.now().strftime("%d%m%Y_%H%M%S")
                                    self.create_csv_file(self.target_resistance_value, timestamp)
                                    self.file_created_for_idx = idx
                                    
                            except Exception as e:
                                print(f"Error parsing ResIdx: {e}")
                        
                        # Check for main data pattern
                        match = self.data_pattern.match(line)
                        if match:
                            values = match.groups()
                            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                            row = [timestamp] + list(values)

                            # Update GUI
                            dials_list = list(self.dials.values())
                            for i, label in enumerate(dials_list):
                                if i < len(values):
                                    label.config(text=values[i])

                            self.iter_label.config(text=values[16])
                            self.pass_label.config(text=values[17])
                            self.mode_label.config(text=values[18])
                            self.current_resistanceLRS_label.config(text=values[27])
                            self.current_resistanceHRS_label.config(text=values[28])
                            self.opstate_label.config(text=values[29])
                            
                            if self.csv_writer:
                                self.csv_writer.writerow(row)
                                self.csv_file.flush()
                        
                        # Handle end of resistance state
                        elif line == '111222333':
                            print(f"\n End of resistance state {self.file_created_for_idx}")
                            if self.csv_file:
                                self.close_csv()
                                
            except Exception as e:
                print(f"Error: {e}")
                
            time.sleep(0.001)

        self.close_csv()

    def stop_reading(self):
        self.is_reading = False
        if self.serial_port:
            self.serial_port.close()
            print("\nSerial port closed")
        self.file_status_label.config(text="Stopped")

    def close_csv(self):
        if self.csv_file:
            filename = self.csv_file.name
            self.csv_file.close()
            self.csv_file = None
            self.csv_writer = None
            filename_short = filename.split('/')[-1]
            print(f" FILE CLOSED: {filename_short}")

if __name__ == "__main__":
    root = tk.Tk()
    app = TeensySerialGUI(root)
    root.mainloop()