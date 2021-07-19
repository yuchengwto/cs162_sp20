#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "page.h"
#include "ram.h"


/* These macros may or may not be useful.
 * */


#define PMD_PFN_MASK 0x000ffffffffff000
#define PTE_PFN_MASK 0x000ffffffffff000
#define PAGE_OFFSET_MASK 0x000ffffffffff000

#define vaddr_pgd(vaddr) vaddr & 0xc0000000
#define vaddr_pmd(vaddr) vaddr & 0x3fe00000
#define vaddr_pte(vaddr) vaddr & 0x001ff000
#define vaddr_off(vaddr) vaddr & 0x00000fff

/* Equal to pfn * page_size. */
#define pfn_to_addr(pfn) (pfn << PAGE_SHIFT)


/* Translates the virtual address vaddr and stores the physical address in paddr.
 * If a page fault occurs, return a non-zero value, otherwise return 0 on a successful translation.
 * */

int virt_to_phys(vaddr_ptr vaddr, paddr_ptr cr3, paddr_ptr *paddr) {
  /* TODO */
  paddr_ptr pdpte_p = cr3 + vaddr_pgd(vaddr);
  uint64_t pdpte;
  ram_fetch(pdpte_p, &pdpte, 8);
  
  paddr_ptr pmd_pfn = pdpte & PMD_PFN_MASK;
  paddr_ptr pde_p = pfn_to_addr(pmd_pfn) + vaddr_pmd(vaddr);
  uint64_t pde;
  ram_fetch(pde_p, &pde, 8);

  paddr_ptr pte_pfn = pde & PTE_PFN_MASK;
  paddr_ptr pte_p = pfn_to_addr(pte_pfn) + vaddr_pte(vaddr);
  uint64_t pte;
  ram_fetch(pte_p, &pte, 8);

  /* Check pte field rationality. */
  if (pte >> 6 & 0x1) {
    if (!(pte >> 1 & 0x1) || !(pte >> 5 & 0x1)) {
      return 1;
    }
  }

  if (pte >> 7 & 0x1) {
    return 1;
  }

  if (pte >> 2 & 0x1) {
    return 1;
  }

  paddr_ptr page_pfn = pte & PAGE_OFFSET_MASK;
  *paddr = pfn_to_addr(page_pfn) + vaddr_off(vaddr);

  return 0;
}

char *str_from_virt(vaddr_ptr vaddr, paddr_ptr cr3) {
  size_t buf_len = 1;
  char *buf = malloc(buf_len);
  char c = ' ';
  paddr_ptr paddr;

  for (int i=0; c; i++) {
    if(virt_to_phys(vaddr + i, cr3, &paddr)){
      printf("Page fault occured at address %p\n", (void *) vaddr + i);
      return (void *) 0;
    }

    ram_fetch(paddr, &c, 1);
    buf[i] = c;
    if (i + 1 >= buf_len) {
      buf_len <<= 1;
      buf = realloc(buf, buf_len);
    }
    buf[i + 1] = '\0';
  }
  return buf;
}

int main(int argc, char **argv) {

  if (argc != 4) {
    printf("Usage: ./mmu <mem_file> <cr3> <vaddr>\n");
    return 1;
  }

  paddr_ptr translated;

  ram_init();
  ram_load(argv[1]);

  paddr_ptr cr3 = strtol(argv[2], NULL, 0);
  vaddr_ptr vaddr = strtol(argv[3], NULL, 0);


  if(virt_to_phys(vaddr, cr3, &translated)){
    printf("Page fault occured at address %p\n", vaddr);
    exit(1);
  }

  char *str = str_from_virt(vaddr, cr3);
  printf("Virtual address %p translated to physical address %p\n", vaddr, translated);
  printf("String representation of data at virtual address %p: %s\n", vaddr, str);

  return 0;
}
