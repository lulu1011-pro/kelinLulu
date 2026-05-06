<script setup lang="ts">
import { computed } from 'vue'

export interface HeadingItem {
  level: number       // 1-6
  text: string        // 标题文本
  slug: string        // 用于锚点跳转的 id
  children: HeadingItem[]
}

const props = defineProps<{
  content: string
  activeSlug?: string
}>()

const emit = defineEmits<{
  scrollTo: [slug: string]
}>()

// 从 Markdown 内容中提取标题
const headings = computed(() => {
  if (!props.content) return []

  const lines = props.content.split('\n')
  const result: HeadingItem[] = []
  const stack: HeadingItem[] = [] // 用于追踪层级

  for (const line of lines) {
    const match = line.match(/^(#{1,6})\s+(.+)$/)
    if (!match) continue

    const level = match[1].length
    const text = match[2].trim()
    // 生成 slug：移除特殊字符，空格替换为 -
    const slug = text
      .toLowerCase()
      .replace(/[^\w\u4e00-\u9fff\s-]/g, '')
      .replace(/\s+/g, '-')
      .replace(/-+/g, '-')
      .replace(/^-|-$/g, '')

    const heading: HeadingItem = { level, text, slug, children: [] }

    // 找到父级：栈中最后一个 level 比当前小的
    while (stack.length > 0 && stack[stack.length - 1].level >= level) {
      stack.pop()
    }

    if (stack.length > 0) {
      stack[stack.length - 1].children.push(heading)
    } else {
      result.push(heading)
    }
    stack.push(heading)
  }

  return result
})

// 扁平化标题列表（用于渲染带缩进的平面列表，更简洁）
const flatHeadings = computed(() => {
  const result: (HeadingItem & { indent: number })[] = []

  function flatten(items: HeadingItem[], depth: number) {
    for (const item of items) {
      result.push({ ...item, indent: depth })
      if (item.children.length > 0) {
        flatten(item.children, depth + 1)
      }
    }
  }

  flatten(headings.value, 0)
  return result
})

function handleClick(slug: string) {
  emit('scrollTo', slug)
}
</script>

<template>
  <div class="outline-panel">
    <div class="outline-header">
      <span class="outline-icon">📑</span>
      <span class="outline-title">大纲</span>
      <span v-if="flatHeadings.length" class="outline-count">{{ flatHeadings.length }}</span>
    </div>

    <div v-if="flatHeadings.length === 0" class="outline-empty">
      <span class="empty-icon">📝</span>
      <p>暂无标题</p>
      <p class="hint">使用 # 标题 生成大纲</p>
    </div>

    <div v-else class="outline-list">
      <button
        v-for="(h, i) in flatHeadings"
        :key="i"
        class="outline-item"
        :class="[
          `level-${h.level}`,
          { active: props.activeSlug === h.slug }
        ]"
        :style="{ paddingLeft: 12 + h.indent * 14 + 'px' }"
        @click="handleClick(h.slug)"
        :title="h.text"
      >
        <span class="heading-marker">{{ '#'.repeat(h.level) }}</span>
        <span class="heading-text">{{ h.text }}</span>
      </button>
    </div>
  </div>
</template>

<style scoped>
.outline-panel {
  display: flex;
  flex-direction: column;
  overflow: hidden;
  height: 100%;
}

.outline-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 10px 12px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.outline-icon {
  font-size: 14px;
}

.outline-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-secondary);
  letter-spacing: 0.3px;
}

.outline-count {
  font-size: 10px;
  color: var(--text-faint);
  background: var(--bg-primary);
  padding: 1px 6px;
  border-radius: 10px;
  min-width: 20px;
  text-align: center;
  font-weight: 500;
}

.outline-empty {
  text-align: center;
  padding: 32px 16px;
  color: var(--text-muted);
}

.outline-empty .empty-icon {
  font-size: 24px;
  display: block;
  margin-bottom: 8px;
  opacity: 0.5;
}

.outline-empty p {
  margin: 4px 0;
  font-size: 13px;
}

.outline-empty .hint {
  font-size: 11px;
  color: var(--text-faint);
}

.outline-list {
  flex: 1;
  overflow-y: auto;
  padding: 4px 0;
}

.outline-item {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
  padding: 5px 12px;
  border: none;
  background: transparent;
  color: var(--text-muted);
  font-size: 12px;
  cursor: pointer;
  text-align: left;
  transition: all var(--duration-fast) var(--ease-out);
  font-family: inherit;
  white-space: nowrap;
  overflow: hidden;
}

.outline-item:hover {
  background: var(--bg-hover);
  color: var(--text-secondary);
}

.outline-item.active {
  color: var(--accent);
  background: var(--accent-glow);
}

.outline-item.active .heading-marker {
  color: var(--accent);
}

.heading-marker {
  font-size: 9px;
  font-weight: 700;
  color: var(--text-faint);
  flex-shrink: 0;
  letter-spacing: -0.5px;
  font-family: 'JetBrains Mono', Consolas, monospace;
  opacity: 0.6;
}

.outline-item:hover .heading-marker {
  opacity: 1;
}

.heading-text {
  overflow: hidden;
  text-overflow: ellipsis;
}

/* 不同级别的样式差异 */
.outline-item.level-1 .heading-text { font-weight: 600; color: var(--text-primary); }
.outline-item.level-1:hover .heading-text,
.outline-item.level-1.active .heading-text { color: var(--accent); }

.outline-item.level-2 .heading-text { font-weight: 500; color: var(--text-secondary); }
.outline-item.level-2:hover .heading-text,
.outline-item.level-2.active .heading-text { color: var(--accent); }

.outline-item.level-3 .heading-text { color: var(--text-muted); }
.outline-item.level-4,
.outline-item.level-5,
.outline-item.level-6 { font-size: 11px; }
.outline-item.level-4 .heading-text,
.outline-item.level-5 .heading-text,
.outline-item.level-6 .heading-text { color: var(--text-faint); }
</style>
