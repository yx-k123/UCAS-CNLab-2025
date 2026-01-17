import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import sys

def plot_cwnd(log_file):
    times = []
    cwnds = []
    ssthreshs = []
    states = []
    
    start_time = None
    
    events_fr_x = []
    events_fr_y = []
    events_to_x = []
    events_to_y = []

    try:
        with open(log_file, 'r') as f:
            lines = f.readlines()
            
        last_state = -1
        last_t = 0
        last_cwnd = 0
        
        for line in lines:
            parts = line.strip().split()
            if len(parts) < 3:
                continue
            
            timestamp = int(parts[0])
            cwnd = int(parts[1])
            ssthresh = int(parts[2])
            state = int(parts[3]) if len(parts) > 3 else 0
            
            if start_time is None:
                start_time = timestamp
            
            # Time in ms
            t = (timestamp - start_time) / 1000.0
            
            times.append(t)
            cwnds.append(cwnd)
            ssthreshs.append(ssthresh if ssthresh < 1000 else None) # Don't plot huge ssthresh
            states.append(state)
            
            # Detect events based on state transition or explicit state
            # TCP_RECOVERY = 2
            # TCP_LOSS = 3
            
            if state == 2 and last_state != 2: # Entering Recovery -> Fast Retransmit
                events_fr_x.append(last_t)
                events_fr_y.append(last_cwnd)
            elif state == 3 and last_state != 3: # Entering Loss -> Timeout
                events_to_x.append(last_t)
                events_to_y.append(last_cwnd)
            
            last_state = state
            last_t = t
            last_cwnd = cwnd

        # Create plot
        plt.figure(figsize=(16, 9))
        ax = plt.gca()
        
        # Plot cwnd
        plt.plot(times, cwnds, marker='.', markersize=4, linestyle='-', linewidth=1.5, label='cwnd (MSS)', color='#1f77b4')
        
        # Plot ssthresh
        # Filter None values for plotting usually requires handling, but pyplot ignores usually?
        # Better to plot separate segments or clean lists. 
        # For simplicity, if ssthresh is None, we just rely on it not showing or valid values.
        # But if it starts None, it might be an issue.
        # Let's replace None with nan which is standard.
        ssthresh_plot = [s if s is not None else float('nan') for s in ssthreshs]
        plt.plot(times, ssthresh_plot, marker='x', markersize=3, linestyle='--', linewidth=1.5, label='ssthresh (MSS)', color='#ff7f0e')
        
        # Plot events
        plt.scatter(events_fr_x, events_fr_y, marker='s', s=50, color='orange', label='Fast Retrans', zorder=5)
        plt.scatter(events_to_x, events_to_y, marker='v', s=60, color='red', label='Timeout', zorder=5)
        
        # Grid settings
        ax.grid(True, which='major', linestyle='-', alpha=0.6)
        ax.grid(True, which='minor', linestyle=':', alpha=0.3)
        ax.minorticks_on()
        
        # Labels and Title
        plt.xlabel('Time (ms)')
        plt.ylabel('Window Size (MSS)')
        plt.title('TCP NewReno Congestion Control\n(Experiement Data)')
        
        # Legend
        plt.legend(loc='upper left')
        
        # Set Y limit to a bit more than max cwnd to look good
        if cwnds:
            max_lines = max(max(cwnds), max([s if s else 0 for s in ssthreshs]))
            # Cap max Y if huge ssthresh was present but transient (unlikely due to filter)
            # Typically cwnd < 30-50 in these labs for readable plots
            plt.ylim(0, max(cwnds) * 1.2 if max(cwnds) > 0 else 10)
        
        plt.savefig('cwnd_plot.png', dpi=100)
        print("Plot saved to cwnd_plot.png")
        
    except FileNotFoundError:
        print(f"Error: File {log_file} not found.")

if __name__ == "__main__":
    log_file = "cwnd_data.txt"
    if len(sys.argv) > 1:
        log_file = sys.argv[1]
    plot_cwnd(log_file)
