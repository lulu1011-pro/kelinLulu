<script setup lang="ts">
interface Shortcut {
  key: string
  description: string
  category: string
}

const props = defineProps<{
  visible: boolean
}>()

const emit = defineEmits<{
  close: []
}>()

const shortcuts: Shortcut[] = [
  // 编辑器快捷键
  { key: 'Ctrl+B', description: '粗体', category: '编辑器' },
  { key: 'Ctrl+I', description: '斜体', category: '编辑器' },
  { key: 'Ctrl+K', description: '插入链接', category: '编辑器' },
  { key: 'Ctrl+Shift+S', description: '删除线', category: '编辑器' },
  { key: 'Ctrl+S', description: '保存笔记', category: '编辑器' },
  { key: 'Ctrl+Z', description: '撤销', category: '编辑器' },
  { key: 'Ctrl+Y', description: '重做', category: '编辑器' },

  // 导航快捷键
  { key: 'Ctrl+K', description: '全局搜索', category: '导航' },
  { key: 'Ctrl+N', description: '新建笔记', category: '导航' },
  { key: 'Ctrl+W', description: '关闭当前标签', category: '导航' },
  { key: 'Ctrl+Tab', description: '切换到下一个标签', category: '导航' },
  { key: 'Ctrl+Shift+Tab', description: '切换到上一个标签', category: '导航' },

  // 视图快捷键
  { key: 'Ctrl+\\', description: '切换侧边栏', category: '视图' },
  { key: 'Ctrl+Shift+L', description: '切换大纲面板', category: '视图' },
  { key: 'Ctrl+Shift+B', description: '切换反向链接面板', category: '视图' },
  { key: 'Ctrl+Shift+P', description: '切换预览模式', category: '视图' },
]

const categories = [...new Set(shortcuts.map(s => s.category))]

function getCategoryShortcuts(category: string) {
  return shortcuts.filter(s => s.category === category)
}
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="shortcut-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="shortcut-modal">
            <div class="shortcut-header">
              <span class="shortcut-icon">⌨️</span>
              <span class="shortcut-title">快捷键</span>
              <button class="btn-close" @click="emit('close')">×</button>
            </div>

            <div class="shortcut-body">
              <div v-for="category in categories" :key="category" class="shortcut-category">
                <h3 class="category-title">{{ category }}</h3>
                <div class="shortcut-list">
                  <div v-for="shortcut in getCategoryShortcuts(category)" :key="shortcut.key" class="shortcut-item">
                    <span class="shortcut-desc">{{ shortcut.description }}</span>
                    <kbd class="shortcut-key">{{ shortcut.key }}</kbd>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.shortcut-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.shortcut-modal {
  width: 500px;
  max-width: 90vw;
  max-height: 80vh;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-xl);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.shortcut-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.shortcut-icon {
  font-size: 18px;
}

.shortcut-title {
  flex: 1;
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary);
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
  transition: all var(--duration-fast);
}

.btn-close:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.shortcut-body {
  flex: 1;
  overflow-y: auto;
  padding: 16px 20px;
}

.shortcut-category {
  margin-bottom: 20px;
}

.shortcut-category:last-child {
  margin-bottom: 0;
}

.category-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin: 0 0 12px 0;
}

.shortcut-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.shortcut-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
}

.shortcut-desc {
  font-size: 13px;
  color: var(--text-secondary);
}

.shortcut-key {
  font-size: 11px;
  padding: 2px 8px;
  background: var(--bg-active);
  border: 1px solid var(--border);
  border-radius: 4px;
  color: var(--text-primary);
  font-family: 'JetBrains Mono', Consolas, monospace;
}
</style>
