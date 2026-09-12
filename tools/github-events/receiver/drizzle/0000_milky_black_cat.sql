CREATE TABLE `events` (
	`id` integer PRIMARY KEY AUTOINCREMENT NOT NULL,
	`delivery` text NOT NULL,
	`received` integer NOT NULL,
	`metadata` text NOT NULL
);
--> statement-breakpoint
CREATE UNIQUE INDEX `events_delivery_unique` ON `events` (`delivery`);
