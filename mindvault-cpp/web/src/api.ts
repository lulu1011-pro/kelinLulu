// MindVault API Client - 封装所有后端 API 调用

const API_BASE = '/api'

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  })
  const json = await res.json()
  if (!json.ok) {
    throw new Error(json.error?.message || 'API Error')
  }
  return json.data as T
}

// ─── Types ───

export interface Note {
  id: number
  title: string
  content?: string
  folder: string
  is_deleted: number
  sort_order?: number
  created_at: string
  updated_at: string
}

export interface Tag {
  id: number
  name: string
  note_count?: number
}

export interface SearchResult {
  id: number
  title: string
  folder: string
  updated_at: string
  title_highlight?: string
  content_highlight?: string
}

// 高亮标记转换：>>>text<<< → <mark>text</mark>
export function highlightToHtml(text?: string): string {
  if (!text) return ''
  return text.replace(/>>>/g, '<mark>').replace(/<<</g, '</mark>')
}

// ─── Notes ───

export const notesApi = {
  list: (folder?: string) =>
    request<Note[]>(`/notes${folder ? `?folder=${encodeURIComponent(folder)}` : ''}`),

  get: (id: number) =>
    request<Note>(`/notes/${id}`),

  create: (data: { title: string; content?: string; folder?: string }) =>
    request<Note>('/notes', { method: 'POST', body: JSON.stringify(data) }),

  update: (id: number, data: { title: string; content: string; folder?: string }) =>
    request<Note>(`/notes/${id}`, { method: 'PUT', body: JSON.stringify(data) }),

  delete: (id: number) =>
    request<void>(`/notes/${id}`, { method: 'DELETE' }),

  trash: () =>
    request<Note[]>('/notes/trash'),

  restore: (id: number) =>
    request<Note>(`/notes/${id}/restore`, { method: 'POST' }),

  permanentDelete: (id: number) =>
    request<void>(`/notes/${id}/permanent`, { method: 'DELETE' }),

  emptyTrash: () =>
    request<{ deleted_count: number }>('/notes/trash/empty', { method: 'DELETE' }),

  backlinks: (id: number) =>
    request<Note[]>(`/notes/${id}/backlinks`),

  links: (id: number) =>
    request<Note[]>(`/notes/${id}/links`),

  updateOrder: (id: number, sortOrder: number) =>
    request<void>(`/notes/${id}/order`, { method: 'PUT', body: JSON.stringify({ sort_order: sortOrder }) }),

  batchUpdateOrder: (orders: { id: number; order: number }[]) =>
    request<void>('/notes/order', { method: 'PUT', body: JSON.stringify({ orders }) }),

  getVersions: (id: number) =>
    request<Version[]>(`/notes/${id}/versions`),

  getVersion: (versionId: number) =>
    request<Version>(`/versions/${versionId}`),
}

export interface Version {
  id: number
  note_id: number
  title: string
  content?: string
  created_at: string
}

// ─── Search ───

export const searchApi = {
  search: (query: string) =>
    request<SearchResult[]>(`/search?q=${encodeURIComponent(query)}`),

  byTitle: (keyword: string) =>
    request<Note[]>(`/search/title?keyword=${encodeURIComponent(keyword)}`),
}

// ─── Tags ───

export const tagsApi = {
  list: () =>
    request<Tag[]>('/tags'),

  addToNote: (noteId: number, name: string) =>
    request<void>(`/notes/${noteId}/tags`, { method: 'POST', body: JSON.stringify({ name }) }),

  removeFromNote: (noteId: number, tagName: string) =>
    request<void>(`/notes/${noteId}/tags`, { method: 'DELETE', body: JSON.stringify({ name: tagName }) }),

  getNoteTags: (noteId: number) =>
    request<Tag[]>(`/notes/${noteId}/tags`),

  getNotesByTag: (tagName: string) =>
    request<Note[]>(`/tags/${encodeURIComponent(tagName)}/notes`),
}

// ─── Files ───

export const filesApi = {
  exportNote: (id: number) =>
    `${API_BASE}/files/export/${id}`,

  importFile: (data: { filename: string; content: string; folder?: string }) =>
    request<Note>('/files/import', { method: 'POST', body: JSON.stringify(data) }),

  uploadImage: async (file: File): Promise<{ url: string; filename: string }> => {
    const formData = new FormData()
    formData.append('file', file)
    const res = await fetch(`${API_BASE}/upload/image`, {
      method: 'POST',
      body: formData,
    })
    const json = await res.json()
    if (!json.ok) throw new Error(json.error?.message || 'Upload failed')
    return json.data as { url: string; filename: string }
  },
}
