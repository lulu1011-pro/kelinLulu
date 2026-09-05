<script setup lang="ts">
import { ref, watch, onBeforeUnmount, nextTick } from 'vue'
import * as d3 from 'd3'
import { notesApi, type Note } from '../api'

const props = defineProps<{
  visible: boolean
}>()

const emit = defineEmits<{
  close: []
  selectNote: [note: Note]
}>()

const graphRef = ref<HTMLDivElement>()
const loading = ref(false)

interface GraphNode {
  id: number
  title: string
  folder: string
  x?: number
  y?: number
  fx?: number | null
  fy?: number | null
}

interface GraphLink {
  source: number | GraphNode
  target: number | GraphNode
}

const nodes = ref<GraphNode[]>([])
const links = ref<GraphLink[]>([])

watch(() => props.visible, (v) => {
  if (v) {
    nextTick(() => loadGraph())
  }
})

async function loadGraph() {
  loading.value = true
  try {
    const allNotes = await notesApi.list()
    
    // Build nodes
    const nodeMap = new Map<number, GraphNode>()
    for (const note of allNotes) {
      nodeMap.set(note.id, {
        id: note.id,
        title: note.title || '无标题',
        folder: note.folder || 'default',
      })
    }

    // Build links from backlinks
    const linkSet = new Set<string>()
    const graphLinks: GraphLink[] = []

    for (const note of allNotes) {
      try {
        const linksData = await notesApi.links(note.id)
        for (const target of linksData) {
          const key = `${note.id}-${target.id}`
          if (!linkSet.has(key)) {
            linkSet.add(key)
            graphLinks.push({ source: note.id, target: target.id })
          }
        }
      } catch (e) {
        // Skip errors
      }
    }

    nodes.value = Array.from(nodeMap.values())
    links.value = graphLinks

    renderGraph()
  } catch (e) {
    console.error('Failed to load graph:', e)
  } finally {
    loading.value = false
  }
}

function renderGraph() {
  if (!graphRef.value) return

  // Clear previous
  d3.select(graphRef.value).selectAll('*').remove()

  const width = graphRef.value.clientWidth
  const height = graphRef.value.clientHeight

  const svg = d3.select(graphRef.value)
    .append('svg')
    .attr('width', width)
    .attr('height', height)

  // Add zoom
  const g = svg.append('g')
  svg.call(d3.zoom<SVGSVGElement, unknown>()
    .extent([[0, 0], [width, height]])
    .scaleExtent([0.1, 4])
    .on('zoom', (event) => {
      g.attr('transform', event.transform)
    }) as any)

  // Create simulation
  const simulation = d3.forceSimulation(nodes.value as any)
    .force('link', d3.forceLink(links.value as any).id((d: any) => d.id).distance(100))
    .force('charge', d3.forceManyBody().strength(-200))
    .force('center', d3.forceCenter(width / 2, height / 2))
    .force('collision', d3.forceCollide().radius(30))

  // Draw links
  const link = g.append('g')
    .attr('class', 'links')
    .selectAll('line')
    .data(links.value)
    .enter()
    .append('line')
    .attr('stroke', 'var(--text-faint)')
    .attr('stroke-opacity', 0.6)
    .attr('stroke-width', 1.5)

  // Draw nodes
  const node = g.append('g')
    .attr('class', 'nodes')
    .selectAll('g')
    .data(nodes.value)
    .enter()
    .append('g')
    .call(d3.drag<any, GraphNode>()
      .on('start', (event, d) => {
        if (!event.active) simulation.alphaTarget(0.3).restart()
        d.fx = d.x
        d.fy = d.y
      })
      .on('drag', (event, d) => {
        d.fx = event.x
        d.fy = event.y
      })
      .on('end', (event, d) => {
        if (!event.active) simulation.alphaTarget(0)
        d.fx = null
        d.fy = null
      }) as any)
    .on('click', (_event, d) => {
      const note = { id: d.id, title: d.title, folder: d.folder } as Note
      emit('selectNote', note)
    })

  // Node circles
  node.append('circle')
    .attr('r', 8)
    .attr('fill', (d: GraphNode) => {
      const colors: Record<string, string> = {
        'default': '#7aa2f7',
        '学习': '#9ece6a',
        '工作': '#ff9e64',
        '生活': '#bb9af7',
        '项目': '#ff9e64',
        '日记': '#bb9af7',
        '想法': '#e0af68',
        '收藏': '#f7768e',
      }
      // 根据文件夹名生成颜色
      if (colors[d.folder]) return colors[d.folder]
      // 为其他文件夹生成随机颜色
      const hash = d.folder.split('').reduce((a, b) => { a = ((a << 5) - a) + b.charCodeAt(0); return a & a }, 0)
      const hue = Math.abs(hash) % 360
      return `hsl(${hue}, 70%, 60%)`
    })
    .attr('stroke', '#1a1b26')
    .attr('stroke-width', 2)

  // Node labels
  node.append('text')
    .text((d: GraphNode) => d.title.length > 10 ? d.title.substring(0, 10) + '...' : d.title)
    .attr('x', 12)
    .attr('y', 4)
    .attr('font-size', '11px')
    .attr('fill', 'var(--text-secondary)')

  // Tooltip
  node.append('title')
    .text((d: GraphNode) => `${d.title}\n文件夹: ${d.folder}`)

  // Simulation tick
  simulation.on('tick', () => {
    link
      .attr('x1', (d: any) => d.source.x)
      .attr('y1', (d: any) => d.source.y)
      .attr('x2', (d: any) => d.target.x)
      .attr('y2', (d: any) => d.target.y)

    node.attr('transform', (d: any) => `translate(${d.x},${d.y})`)
  })
}

onBeforeUnmount(() => {
  if (graphRef.value) {
    d3.select(graphRef.value).selectAll('*').remove()
  }
})
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="graph-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="graph-modal">
            <div class="graph-header">
              <span class="graph-icon">🕸️</span>
              <span class="graph-title">知识图谱</span>
              <span class="graph-hint">拖拽节点 · 点击打开 · 滚轮缩放</span>
              <button class="btn-close" @click="emit('close')">×</button>
            </div>

            <div class="graph-body">
              <div v-if="loading" class="graph-loading">
                加载中...
              </div>
              <div v-else-if="nodes.length === 0" class="graph-empty">
                暂无笔记数据
              </div>
              <div ref="graphRef" class="graph-canvas"></div>
            </div>

            <div class="graph-footer">
              <div class="legend">
                <span class="legend-item">
                  <span class="legend-dot" style="background: #7aa2f7"></span> default
                </span>
                <span class="legend-item">
                  <span class="legend-dot" style="background: #9ece6a"></span> 学习
                </span>
                <span class="legend-item">
                  <span class="legend-dot" style="background: #ff9e64"></span> 工作
                </span>
                <span class="legend-item">
                  <span class="legend-dot" style="background: #bb9af7"></span> 生活
                </span>
                <span class="legend-item">
                  <span class="legend-dot" style="background: #e0af68"></span> 想法
                </span>
                <span class="legend-item">
                  <span class="legend-dot" style="background: #f7768e"></span> 收藏
                </span>
                <span class="legend-item">
                  <span class="legend-dot" style="background: #73daca"></span> 其他
                </span>
              </div>
              <span class="graph-count">{{ nodes.length }} 个笔记 · {{ links.length }} 条链接</span>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.graph-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.graph-modal {
  width: 90vw;
  max-width: 1200px;
  height: 80vh;
  max-height: 800px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-xl);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.graph-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.graph-icon {
  font-size: 18px;
}

.graph-title {
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary);
}

.graph-hint {
  font-size: 11px;
  color: var(--text-muted);
  margin-left: 12px;
}

.btn-close {
  width: 28px;
  height: 28px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-muted);
  font-size: 18px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-left: auto;
  transition: all var(--duration-fast);
}

.btn-close:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.graph-body {
  flex: 1;
  position: relative;
  overflow: hidden;
}

.graph-loading,
.graph-empty {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  font-size: 14px;
}

.graph-canvas {
  width: 100%;
  height: 100%;
  cursor: grab;
}

.graph-canvas:active {
  cursor: grabbing;
}

.graph-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 20px;
  border-top: 1px solid var(--border);
  flex-shrink: 0;
}

.legend {
  display: flex;
  gap: 16px;
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: var(--text-muted);
}

.legend-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
}

.graph-count {
  font-size: 11px;
  color: var(--text-faint);
}
</style>
