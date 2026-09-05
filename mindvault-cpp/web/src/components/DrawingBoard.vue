<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, nextTick } from 'vue'
import { filesApi } from '../api'

interface Props {
  visible: boolean
  initialData?: string
}

const props = defineProps<Props>()
const emit = defineEmits<{
  (e: 'close'): void
  (e: 'save', url: string): void
}>()

const canvasRef = ref<HTMLCanvasElement | null>(null)
const ctx = ref<CanvasRenderingContext2D | null>(null)
const isDrawing = ref(false)
const lastX = ref(0)
const lastY = ref(0)

const tool = ref<'pen' | 'eraser'>('pen')
const color = ref('#ffffff')
const lineWidth = ref(3)

// 预设颜色
const presetColors = ['#ffffff', '#ff6b6b', '#ffa94d', '#ffd43b', '#69db7c', '#38d9a9', '#4dabf7', '#748ffc', '#da77f2', '#495057']
const lineWidths = [2, 4, 6, 10, 16]

// 历史记录（使用 ImageData 提高性能）
const history = ref<ImageData[]>([])
const historyIndex = ref(-1)

onMounted(() => initCanvas())

function initCanvas() {
  if (canvasRef.value) {
    const canvas = canvasRef.value
    canvas.width = canvas.offsetWidth || 800
    canvas.height = canvas.offsetHeight || 600
    ctx.value = canvas.getContext('2d')
    if (ctx.value) {
      ctx.value.fillStyle = '#1a1b26'
      ctx.value.fillRect(0, 0, canvas.width, canvas.height)
      saveState()
    }
  }
}

watch(() => props.visible, (val) => {
  if (val) {
    nextTick(() => {
      initCanvas()
      if (props.initialData) loadFromDataUrl(props.initialData)
    })
  }
})

function saveState() {
  if (!ctx.value || !canvasRef.value) return
  const imageData = ctx.value.getImageData(0, 0, canvasRef.value.width, canvasRef.value.height)
  history.value = history.value.slice(0, historyIndex.value + 1)
  history.value.push(imageData)
  historyIndex.value = history.value.length - 1
}

function restoreState() {
  if (!ctx.value || !history.value[historyIndex.value]) return
  ctx.value.putImageData(history.value[historyIndex.value], 0, 0)
}

function undo() {
  if (historyIndex.value > 0) {
    historyIndex.value--
    restoreState()
  }
}

function redo() {
  if (historyIndex.value < history.value.length - 1) {
    historyIndex.value++
    restoreState()
  }
}

function startDrawing(e: MouseEvent) {
  if (!canvasRef.value) return
  isDrawing.value = true
  const rect = canvasRef.value.getBoundingClientRect()
  lastX.value = e.clientX - rect.left
  lastY.value = e.clientY - rect.top
  ctx.value?.beginPath()
  ctx.value?.moveTo(lastX.value, lastY.value)
}

function draw(e: MouseEvent) {
  if (!isDrawing.value || !ctx.value || !canvasRef.value) return
  const rect = canvasRef.value.getBoundingClientRect()
  const x = e.clientX - rect.left
  const y = e.clientY - rect.top

  ctx.value.strokeStyle = tool.value === 'eraser' ? '#1a1b26' : color.value
  ctx.value.lineWidth = tool.value === 'eraser' ? lineWidth.value * 3 : lineWidth.value
  ctx.value.lineCap = 'round'
  ctx.value.lineJoin = 'round'

  // 直接用 lineTo 连接线段，保持连续
  ctx.value.lineTo(x, y)
  ctx.value.stroke()
  ctx.value.beginPath()
  ctx.value.moveTo(x, y)

  lastX.value = x
  lastY.value = y
}

function stopDrawing() {
  if (isDrawing.value) {
    isDrawing.value = false
    ctx.value?.closePath()
    saveState()
  }
}

function clearCanvas() {
  if (!ctx.value || !canvasRef.value) return
  ctx.value.fillStyle = '#1a1b26'
  ctx.value.fillRect(0, 0, canvasRef.value.width, canvasRef.value.height)
  saveState()
}

async function handleSave() {
  if (!canvasRef.value) return
  const dataUrl = canvasRef.value.toDataURL('image/png')
  try {
    const { url } = await filesApi.uploadBase64(dataUrl)
    emit('save', url)
  } catch {
    emit('save', dataUrl)
  }
}

function handleClose() { emit('close') }

function loadFromDataUrl(dataUrl: string) {
  if (!ctx.value || !canvasRef.value) return
  const canvas = canvasRef.value
  const img = new Image()
  img.onload = () => {
    if (!ctx.value) return
    ctx.value.clearRect(0, 0, canvas.width, canvas.height)
    ctx.value?.drawImage(img, 0, 0)
    saveState()
  }
  img.src = dataUrl
}

function handleKeydown(e: KeyboardEvent) {
  if (e.ctrlKey || e.metaKey) {
    if (e.key === 'z') { e.preventDefault(); undo() }
    else if (e.key === 'y') { e.preventDefault(); redo() }
  }
}

onMounted(() => window.addEventListener('keydown', handleKeydown))
onUnmounted(() => window.removeEventListener('keydown', handleKeydown))
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="drawing-overlay" @click.self="handleClose">
        <Transition name="modal">
          <div v-if="visible" class="drawing-container">
            <!-- 工具栏 -->
            <div class="drawing-header">
              <div class="toolbar-left">
                <button :class="['tool-btn', { active: tool === 'pen' }]" @click="tool = 'pen'" title="画笔">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 3a2.85 2.85 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5Z"/></svg>
                </button>
                <button :class="['tool-btn', { active: tool === 'eraser' }]" @click="tool = 'eraser'" title="橡皮擦">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21"/><path d="M22 21H7"/><path d="m5 11 9 9"/></svg>
                </button>
              </div>

              <div class="toolbar-colors">
                <button v-for="c in presetColors" :key="c" :class="['color-btn', { active: color === c }]" :style="{ background: c }" @click="color = c"></button>
              </div>

              <div class="toolbar-widths">
                <button v-for="w in lineWidths" :key="w" :class="['width-btn', { active: lineWidth === w }]" @click="lineWidth = w" :title="`${w}px`">
                  <span class="width-dot" :style="{ width: w + 2 + 'px', height: w + 2 + 'px' }"></span>
                </button>
              </div>

              <div class="toolbar-right">
                <button class="tool-btn" @click="undo" title="撤销 (Ctrl+Z)">
                  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 7v6h6"/><path d="M21 17a9 9 0 0 0-9-9 9 9 0 0 0-6 2.3L3 13"/></svg>
                </button>
                <button class="tool-btn" @click="redo" title="重做 (Ctrl+Y)">
                  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 7v6h-6"/><path d="M3 17a9 9 0 0 1 9-9 9 9 0 0 1 6 2.3L21 13"/></svg>
                </button>
                <button class="tool-btn" @click="clearCanvas" title="清空">
                  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/></svg>
                </button>
                <div class="sep"></div>
                <button class="action-btn cancel" @click="handleClose">取消</button>
                <button class="action-btn save" @click="handleSave">插入笔记</button>
              </div>
            </div>

            <!-- 画布 -->
            <div class="drawing-canvas-container">
              <canvas
                ref="canvasRef"
                class="drawing-canvas"
                @mousedown="startDrawing"
                @mousemove="draw"
                @mouseup="stopDrawing"
                @mouseleave="stopDrawing"
              ></canvas>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.drawing-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.7);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.drawing-container {
  width: 90vw;
  height: 90vh;
  background: var(--bg-secondary);
  border-radius: 12px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  box-shadow: 0 16px 48px rgba(0,0,0,0.3);
}

.drawing-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 16px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-primary);
  flex-shrink: 0;
}

.toolbar-left, .toolbar-right {
  display: flex;
  align-items: center;
  gap: 4px;
}

.tool-btn {
  width: 32px;
  height: 32px;
  border: 1px solid transparent;
  border-radius: 6px;
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.15s;
}

.tool-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
.tool-btn.active { background: var(--accent-glow); color: var(--accent); border-color: rgba(122,162,247,0.3); }

.toolbar-colors {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 0 12px;
  border-left: 1px solid var(--border);
  border-right: 1px solid var(--border);
}

.color-btn {
  width: 22px;
  height: 22px;
  border-radius: 50%;
  border: 2px solid transparent;
  cursor: pointer;
  transition: all 0.15s;
  box-sizing: border-box;
}

.color-btn:hover { transform: scale(1.15); }
.color-btn.active { border-color: var(--accent); }

.toolbar-widths {
  display: flex;
  align-items: center;
  gap: 8px;
}

.width-btn {
  width: 28px;
  height: 28px;
  border: 1px solid transparent;
  border-radius: 6px;
  background: transparent;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.15s;
}

.width-btn:hover { background: var(--bg-hover); }
.width-btn.active { border-color: rgba(122,162,247,0.3); background: var(--accent-glow); }

.width-dot {
  display: block;
  border-radius: 50%;
  background: var(--text-secondary);
  transition: all 0.15s;
}

.sep {
  width: 1px;
  height: 24px;
  background: var(--border);
  margin: 0 4px;
}

.action-btn {
  padding: 6px 14px;
  border: none;
  border-radius: 6px;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
}

.action-btn.cancel {
  background: transparent;
  color: var(--text-secondary);
}

.action-btn.cancel:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.action-btn.save {
  background: var(--accent);
  color: #fff;
}

.action-btn.save:hover {
  background: var(--accent-hover);
}

.drawing-canvas-container {
  flex: 1;
  overflow: hidden;
  background: #1a1b26;
}

.drawing-canvas {
  width: 100%;
  height: 100%;
  cursor: crosshair;
}

/* 过渡动画 */
.overlay-enter-active, .overlay-leave-active { transition: opacity 0.2s; }
.overlay-enter-from, .overlay-leave-to { opacity: 0; }
.modal-enter-active, .modal-leave-active { transition: transform 0.2s, opacity 0.2s; }
.modal-enter-from, .modal-leave-to { transform: scale(0.95); opacity: 0; }
</style>
