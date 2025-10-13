const margin = { top: 30, right: 20, bottom: 30, left: 140 };
const width = 1600 - margin.left - margin.right;
const height = 1000 - margin.top - margin.bottom;

const svg = d3.select("#timeline")
  .append("svg")
  .attr("width", width + margin.left + margin.right)
  .attr("height", height + margin.top + margin.bottom);

const g = svg.append("g").attr("transform", `translate(${margin.left},${margin.top})`);

// Arrowhead for flow
svg.append("defs").append("marker")
  .attr("id", "arrowhead")
  .attr("viewBox", "-0 -5 10 10")
  .attr("refX", 5)
  .attr("refY", 0)
  .attr("orient", "auto")
  .attr("markerWidth", 6)
  .attr("markerHeight", 6)
  .append("path")
  .attr("d", "M 0,-5 L 10 ,0 L 0,5")
  .attr("fill", "#f00");

let events = [];
let flows = {};
let filteredEvents = [];
let xScale = d3.scaleLinear().range([0, width]);
let yScale = d3.scaleLinear().range([0, height]);

// === 全局状态（持久化）===
let coreMap = {};
let threadMap = {};
let coreThreads = {}; // core -> Set(tid)
let intervals = [];   // 所有已解析的 interval
let trackY = {};      // trackLabel -> y position
let nextY = 0;        // 下一个可用的 Y 位置（用于新增 track）
const threadHeight = 20;
// 自定义缩放：仅允许平移（禁止默认缩放）
// Zoom 行为：只允许平移（缩放比例固定为 1）
const panZoom = d3.zoom()
  .scaleExtent([1, 1]) // 锁定不能缩放
  .translateExtent([[-Infinity, -Infinity], [Infinity, Infinity]])
  .wheelDelta(function(event) {
    // 减慢 Alt+滚轮的缩放速度
    return event.altKey ? -event.deltaY * 0.0008 : -event.deltaY;
  })
  .on("zoom", (event) => {
    // 仅平移（拖拽）
    transform = event.transform;
    g.attr("transform", transform);
    renderAxes();
  });

svg.call(panZoom);
let transform = d3.zoomIdentity;
let xaltscale = 1; // 当alt + wheel的缩放
let autoReloadTimer = null;

// Initialize Socket.IO
const socket = io();
// 全局 stack（用于 B/E 匹配）
let globalStack = {};

// 初始加载
socket.on("init_events", (data) => {
  // 重置状态
  events = [];
  filteredEvents = [];
  coreMap = {};
  threadMap = {};
  coreThreads = {};
  intervals = [];
  flows = {};
  globalStack = {};
  trackY = {};
  nextY = 0;

  events = data;
  filteredEvents = [...events];
  processEvents(filteredEvents); // 全量处理
  render(); // 首次全量渲染
});

// 新事件到来
socket.on("new_events", (newEvents) => {
  events = [...events, ...newEvents];

  // 只处理新事件
  const { newlyAddedIntervals, newlyAddedFlows } = processEvents(newEvents);

  // 应用过滤（只过滤新事件？或重新过滤？）
  // 简单起见：重新过滤（但可优化）
  filterEvents();

  // 触发渲染（现在 render 只做图形更新）
  render();
});

// clear
socket.on("clear_events", () => {
  // 重置所有状态
  events = [];
  filteredEvents = [];
  coreMap = {};
  threadMap = {};
  coreThreads = {};
  intervals = [];
  flows = {};
  globalStack = {};
  trackY = {};
  nextY = 0;
  document.getElementById("searchInput").value = "";
  render(); // 清空画面
});


// 处理一批事件（可以是 init 或 new_events），增量更新全局状态
function processEvents(newEvents) {
  const newlyAddedIntervals = [];
  const newlyAddedFlows = [];

  for (const e of newEvents) {
    // === 处理 metadata (M) ===
    if (e.ph === "M" && e.name === "process_name") {
      coreMap[e.pid] = e.args.name;
    } else if (e.ph === "M" && e.name === "thread_name") {
      const key = `${e.pid}-${e.tid}`;
      threadMap[key] = e.args.name;
      const core = coreMap[e.pid] || `Core ${e.pid}`;
      if (!coreThreads[core]) coreThreads[core] = new Set();
      coreThreads[core].add(e.tid);
    }

    // === 处理 B/E/X 事件 ===
    if (e.ph === "B") {
      // 暂存到全局 stack（需持久化）
      if (!globalStack) globalStack = {};
      globalStack[`${e.pid}-${e.tid}-${e.name}-${e.ts}`] = e;
    } else if (e.ph === "E") {
      // 尝试匹配 B
      for (const key in globalStack) {
        const b = globalStack[key];
        if (b.name === e.name && b.pid === e.pid && b.tid === e.tid) {
          const interval = createIntervalFromPair(b, e);
          intervals.push(interval);
          newlyAddedIntervals.push(interval);
          delete globalStack[key];
          break;
        }
      }
    } else if (e.ph === "X" && e.dur !== undefined) {
      const interval = createIntervalFromX(e);
      intervals.push(interval);
      newlyAddedIntervals.push(interval);
    }

    // === 处理 flow (s/f) ===
    if (e.ph === "s" || e.ph === "f") {
      if (!flows[e.id]) flows[e.id] = {};
      flows[e.id][e.ph] = e;
      // 如果 s 和 f 都齐了，且是新完成的 flow
      if (flows[e.id].s && flows[e.id].f) {
        newlyAddedFlows.push(flows[e.id]);
      }
    }
  }

  // === 处理新增的 track（线程）===
  // 检查 newlyAddedIntervals 中是否有新 trackLabel
  const newTrackLabels = new Set();
  for (const iv of newlyAddedIntervals) {
    if (trackY[iv.trackLabel] === undefined) {
      newTrackLabels.add(iv.trackLabel);
    }
  }

  if (newTrackLabels.size > 0) {
    // 重新计算所有 trackY（简单起见，也可增量分配）
    recalculateTrackY();
  }

  return { newlyAddedIntervals, newlyAddedFlows };
}

function createIntervalFromPair(b, e) {
  const coreName = coreMap[b.pid] || `Core ${b.pid}`;
  const threadKey = `${b.pid}-${b.tid}`;
  const threadName = threadMap[threadKey] || `TID ${b.tid}`;
  return {
    ...b,
    dur: e.ts - b.ts,
    coreName,
    threadName,
    trackLabel: `${coreName} / ${threadName}`,
    id: `${b.pid}-${b.tid}-${b.ts}`
  };
}

function createIntervalFromX(e) {
  const coreName = coreMap[e.pid] || `Core ${e.pid}`;
  const threadKey = `${e.pid}-${e.tid}`;
  const threadName = threadMap[threadKey] || `TID ${e.tid}`;
  return {
    ...e,
    coreName,
    threadName,
    trackLabel: `${coreName} / ${threadName}`,
    id: `${e.pid}-${e.tid}-${e.ts}`
  };
}

// 重新计算所有 track 的 Y 位置（当有新线程出现时）
function recalculateTrackY() {
  const cores = Object.keys(coreThreads).sort();
  let currentY = 0;
  const newTrackY = {};
  for (const core of cores) {
    const pids = Object.keys(coreMap).filter(pid => coreMap[pid] === core);
    const threads = [];
    for (const pid of pids) {
      for (const tid of coreThreads[core]) {
        const key = `${pid}-${tid}`;
        if (threadMap[key]) {
          threads.push({ pid: Number(pid), tid, name: threadMap[key] });
        }
      }
    }
    threads.sort((a, b) => a.tid - b.tid);
    for (const t of threads) {
      const label = `${core} / ${t.name}`;
      newTrackY[label] = currentY;
      currentY += threadHeight;
    }
  }
  trackY = newTrackY;
  nextY = currentY;
}
function filterEvents() {
  const query = document.getElementById("searchInput").value.toLowerCase();
  if (!query) {
    filteredEvents = [...events];
  } else {
    filteredEvents = events.filter(e => e.name && e.name.toLowerCase().includes(query));
  }
  render();
}

function resetZoom() {
  xaltscale = 1;
  transform = d3.zoomIdentity;
  g.attr("transform", transform);
  render();
}

function toggleAutoReload() {
  const enabled = document.getElementById("autoReload").checked;
  if (enabled) {
    autoReloadTimer = setInterval(() => {
      filterEvents();
    }, 2000);
  } else {
    clearInterval(autoReloadTimer);
  }
}

// 全局监听滚轮
svg.on("wheel", function(event) {
  if (event.altKey) {
    // === Alt + 滚轮：仅 X 轴缩放 ===
    event.preventDefault();

    const scaleFactor = event.deltaY > 0 ? 0.5 : 2; // 更平滑（每次 5% 变化）
    xaltscale *= scaleFactor;
    xaltscale = Math.max(0.01, Math.min(100, xaltscale));
    xaltscale = Number(xaltscale.toFixed(1)); // 👈 限制为 1 位小数
    render();
  }else if (event.ctrlKey) {
    event.preventDefault();

    const scaleFactor = event.deltaY > 0 ? 0.95 : 1.05; // 更平滑（每次 5% 变化）
    const newK = Math.max(0.1, Math.min(10, transform.k * scaleFactor));

    // 获取鼠标在 SVG 内的位置（用于缩放中心）
    const svgRect = svg.node().getBoundingClientRect();
    const mouseX = event.clientX - svgRect.left - margin.left;

    // 当前缩放下，鼠标对应的原始时间值（数据域）
    const xScaleZoom = transform.rescaleX(xScale);
    const timeAtMouse = xScaleZoom.invert(mouseX);

    // 计算新的 x 平移，使得 timeAtMouse 在缩放后仍在 mouseX 位置
    const newX = mouseX - xScale(timeAtMouse) * newK;

    // 更新 transform：只改 X 平移和缩放，Y 保持不变！
    transform = d3.zoomIdentity
      .translate(newX, transform.y) // 👈 Y 保持原值
      .scale(newK);

    g.attr("transform", transform);
    renderAxes();
  } else {
    // === 普通滚轮：Y 轴平移 ===
    event.preventDefault();
    const dy = -event.deltaY * 0.4; // 平移速度
    transform = d3.zoomIdentity
      .translate(transform.x, transform.y + dy)
      .scale(transform.k);
    g.attr("transform", transform);
    renderAxes();
  }
});

// 全局变量：记录已渲染的事件 ID
let renderedEvents = new Set();
let renderedFlows = new Set();

function render() {
  // 1. 更新 X scale domain（基于所有 intervals）
  if (intervals.length === 0) {
    g.selectAll(".event, .flow-arrow, .track-label, .axis").remove();
    return;
  }

  const allTs = intervals.flatMap(e => [e.ts, e.ts + (e.dur || 0)]);
  const minTs = Math.min(...allTs), maxTs = Math.max(...allTs);
  xScale.domain([minTs, maxTs * xaltscale || minTs + 1]);
  const xScaleZoom = transform.rescaleX(xScale);

  // 2. 更新 track labels（全量，但 cheap）
  const trackEntries = Object.entries(trackY);
  const existingLabels = g.selectAll(".track-label").data(trackEntries);
  existingLabels.exit().remove();
  existingLabels.enter()
    .append("text")
    .attr("class", "track-label")
    .merge(existingLabels)
    .attr("x", -10)
    .attr("y", ([label, y]) => y + threadHeight / 2)
    .attr("dy", "0.35em")
    .attr("text-anchor", "end")
    .text(([label]) => label);

  // 3. 更新 events（D3 自动增量）
  const bars = g.selectAll(".event").data(intervals, d => d.id);
  bars.enter()
    .append("rect")
    .attr("class", "event")
    .merge(bars)
    .attr("x", d => xScaleZoom(d.ts))
    .attr("y", d => trackY[d.trackLabel])
    .attr("width", d => Math.max(1, xScaleZoom(d.ts + d.dur) - xScaleZoom(d.ts)))
    .attr("height", threadHeight)
    .attr("fill", d => {
      let hash = 0;
      for (let i = 0; i < d.name.length; i++) hash = d.name.charCodeAt(i) + ((hash << 5) - hash);
      return "#" + (hash & 0x00FFFFFF).toString(16).padStart(6, '0');
    })
    .each(function(d) {
      const title = d3.select(this).select("title");
      const text = `${d.name} [${d.ts.toFixed(3)} - ${(d.ts + d.dur).toFixed(3)}]`;
      if (title.empty()) {
        d3.select(this).append("title").text(text);
      } else {
        title.text(text);
      }
    });
  bars.exit().remove();

  // 4. 更新 flows
  const flowData = Object.values(flows).filter(f => f.s && f.f);
  const arrows = g.selectAll(".flow-arrow").data(flowData, d => d.s.id);
  arrows.enter()
    .append("line")
    .attr("class", "flow-arrow")
    .merge(arrows)
    .attr("x1", d => xScaleZoom(d.s.ts))
    .attr("y1", d => {
      const core = coreMap[d.s.pid] || `Core ${d.s.pid}`;
      const thread = threadMap[`${d.s.pid}-${d.s.tid}`] || `TID ${d.s.tid}`;
      return trackY[`${core} / ${thread}`] + threadHeight / 2;
    })
    .attr("x2", d => xScaleZoom(d.f.ts))
    .attr("y2", d => {
      const core = coreMap[d.f.pid] || `Core ${d.f.pid}`;
      const thread = threadMap[`${d.f.pid}-${d.f.tid}`] || `TID ${d.f.tid}`;
      return trackY[`${core} / ${thread}`] + threadHeight / 2;
    });
  arrows.exit().remove();

  // 5. 更新坐标轴
  renderAxes();
}

function renderAxes() {
  g.selectAll(".axis").remove();
  g.append("g")
    .attr("class", "axis")
    .attr("transform", `translate(0,${height})`)
    .call(d3.axisBottom(xScale).tickFormat(d3.format(".3f")));
}
// 新增：日志相关
const logContent = document.getElementById("logContent");

// 监听日志消息
socket.on("log_message", (msg) => {
  const line = document.createElement("div");
  line.textContent = msg;
  logContent.appendChild(line);
  // 自动滚动到底部
  logContent.scrollTop = logContent.scrollHeight;
});

function clearLog() {
  logContent.innerHTML = "";
}
// New function: run simulation via API
function runSimulation() {
  const configFile = document.getElementById("configFile").value;
  const coreConfigFile = document.getElementById("coreConfigFile").value;
  const statusDiv = document.getElementById("simStatus");

  statusDiv.textContent = "Running...";
  statusDiv.style.color = "#007bff";

  fetch("/run-simulation", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      config_file: configFile,
      core_config_file: coreConfigFile
    })
  })
  .then(res => res.json())
  .then(data => {
    if (data.error) {
      statusDiv.textContent = "❌ Error: " + data.error;
      statusDiv.style.color = "#dc3545";
    } else {
      statusDiv.textContent = "✅ Started (PID: " + data.pid + ")";
      statusDiv.style.color = "#28a745";
    }
  })
  .catch(err => {
    statusDiv.textContent = "❌ Network error";
    statusDiv.style.color = "#dc3545";
  });
}

// Initial load
window.onload = () => {
  // Wait for socket init
};

// 页面加载时恢复状态
window.addEventListener("load", () => {
  const saved = localStorage.getItem("hideLog");
  const hideLog = saved !== null ? (saved === "true") : true; // 默认 true
  document.getElementById("hideLog").checked = hideLog;
  toggleLogVisibility();
});

// 全局变量：当前是否隐藏日志
let isLogHidden = false;

function toggleLogVisibility() {
  isLogHidden = document.getElementById("hideLog").checked;
  
  // 隐藏/显示面板
  const logPanel = document.getElementById("logPanel");
  logPanel.style.display = isLogHidden ? "none" : "flex";
  
  // 发送状态到后端
  socket.emit("log_visibility", isLogHidden);
}


function clearTrace() {
  if (!confirm("Are you sure you want to clear all trace events?")) {
    return;
  }

  fetch("/clear-trace", { method: "POST" })
    .then(res => res.json())
    .then(data => {
      if (data.status === "success") {
        // 清空前端事件
        events = [];
        filteredEvents = [];
        render(); // 会清空所有图形
        document.getElementById("searchInput").value = ""; // 清空搜索框
        alert("Trace cleared successfully!");
      } else {
        alert("Error: " + data.error);
      }
    })
    .catch(err => {
      alert("Network error: " + err);
    });
}