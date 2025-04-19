// @ts-ignore
import { execSync } from "child_process";

export const javap = (file: string): string => {
	return execSync(`javap -public ${file}`).toString();
};
