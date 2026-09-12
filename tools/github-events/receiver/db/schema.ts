import { sqliteTable, integer, text } from 'drizzle-orm/sqlite-core';
export const events = sqliteTable('events', {
  id: integer('id').primaryKey({ autoIncrement: true }),
  delivery: text('delivery').notNull().unique(),
  received: integer('received').notNull(),
  metadata: text('metadata').notNull()
});
