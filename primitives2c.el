;;$Id$
;;Copyright 1992 by the ANSI figForth Development Group

;; To Do:
;; rewrite in Forth (using regexp)
;; cleanup wrt. float vs. cell and load vs. store

;Optimizations:
;superfluous stores are removed. GCC removes the superfluous loads by itself
;TOS and FTOS can be kept in register( variable)s.
;
;Problems:
;The TOS optimization is somewhat hairy. The problems by example:
;1) dup ( w -- w w ): w=TOS; sp-=1; sp[1]=w; TOS=w;
;   The store is not superfluous although the earlier opt. would think so
;   Alternatively:    sp[0]=TOS; w=TOS; sp-=1; TOS=w;
;2) ( -- .. ): sp[0] = TOS; ... /* This additional store is necessary */
;3) ( .. -- ): ... TOS = sp[0]; /* as well as this load */
;4) ( -- ): /* but here they are unnecessary */
;5) Words that call NEXT themselves have to be done very carefully.

(setq max-lisp-eval-depth 1000)

(defun filter-primitives (source destination filter)
  "convert source into destination using filter"
  (switch-to-buffer (generate-new-buffer destination))
  (insert-file source)
  (insert (format "/* File generated by $RCSfile$ (%s) from %s */\n" filter source))
  (filter-primitives1 filter)
  (write-file destination))

(defun match-string (N)
"Returns the string of the Nth match"
  (let ((b (match-beginning N))
	(e (match-end N)))
    (cond (b (buffer-substring b e))
	  (t ""))))

(defun filter-primitives1 (filter)
  "replace primitives in the rest of the buffer with filtered primitives"
  (cond ((re-search-forward "^\\([^ \t\n]+\\)\t+\\([^\t\n]*--[^\t\n]*\\)\t+\\([^\t\n]+\\)\t*\\([^ \t\n]+\\)?\n\\(\"\"[^\"]*\"\"\n\\)?\\(\\([^:\n].*\n\\)*\\)\\(:\n\\(\\(.+\n\\)*\\)\\)?$" nil t)
;\\(:\n\\(\\(.+\n\\)*\\)\\)?
	 (replace-match
	  (funcall filter 
	   (match-string 1)
	   (match-string 2)
	   (match-string 3)
	   (cond ((equal (match-string 4) "") (match-string 1))
		 (t (match-string 4)))
	   (match-string 5)
	   (match-string 7)
	   (match-string 9)
	   ))
	 (filter-primitives1 filter))))

(defun list-filter (forth-name stack-effect standards c-name doku code forth-code)
  (format "&&I_%s," c-name))

(defvar primitive-number 0)

(defun alias-filter (forth-name stack-effect standards c-name doku code forth-code)
  (setq primitive-number (+ 1 primitive-number))
  (format "%s Alias %s" (- -5 primitive-number) forth-name))

(defun c-filter (forth-name stack-effect standards c-name doku code forth-code)
  "c code for the primitive"
  (let ((effects (parse-stack-effect stack-effect)))
    (format "I_%s:	/* %s ( %s ) */\n/* %s */\n{\nLabel ca;\n%s\nNAME(\"%s\")\n{\n%s}\nNEXT_P1;\n%sNEXT1_P2;\n}\n"
	    c-name forth-name stack-effect doku
	    (prefix effects) forth-name code (suffix effects))))

(defun forth-filter (forth-name stack-effect standards c-name doku code forth-code)
  "forth equivalent for the primitive"
  ;should other info be included?
  (cond ((equal forth-code "") "")
	(t (format ": %s ( %s )\n%s" forth-name stack-effect forth-code))))

(defun prefix (effects)
  "c-code for declaring vars and getting them from the stack"
  (format "%s%s%ssp += %s;\nfp += %s;\n"
	  (declarations (unique (append (effect-in effects) (effect-out effects))))
	  (store-tos effects)
	  (loads (effect-in effects))
	  (effect-cells effects)
	  (effect-floats effects)))

(defun suffix (effects)
  "c code for storing vars to the stack"
  (format "%s%s"
	  (stores (effect-out effects) effects)
	  (load-tos effects)))

(defun unique (set)
  "the set with duplicates removed"
  (cond ((null set) nil)
	((memq (car set) (cdr set)) (unique (cdr set)))
	(t (cons (car set) (unique (cdr set))))))

(defun cells (vars)
  "the number of stack cells needed by vars"
  (cond ((null vars) 0)
	(t (+ (cond ((float-type (car vars)) 0)
		    ((double-type (car vars)) 2)
		    (t 1))
	      (cells (cdr vars))))))

(defun floats (vars)
  "the number of floating-point stack items needed by vars"
  (cond ((null vars) 0)
	(t (+ (cond ((float-type (car vars)) 1)
		    (t 0))
	      (floats (cdr vars))))))

(defun declarations (vars)
  "C declarations for vars"
  (mapconcat '(lambda (var) (format "%s	%s;\n" (type var) var)) vars ""))

(defun regexp-assoc (var ralist)
  (cond ((null ralist) (error "%s does not match" var))
	((string-match (caar ralist) var) (cadar ralist))
	(t (regexp-assoc var (cdr ralist)))))


(defun type (var)
  "a declaration for var"
  (let ((data (match-data)))
    (unwind-protect 
	(regexp-assoc (format "%s" var)
		      '(("^a_" "Cell *")
			("^c_" "Char *")
			("^df_" "DFloat *")
			("^sf_" "SFloat *")
			("^f_" "Float *")
			("^xt" Xt)
			("^wid" Wid)
			("^f83name" "F83Name *")
			("^ud" UDCell)
			("^r" Float)
			("^f" Bool)
			("^c" Char)
			("^[nw]" Cell)
			("^u" UCell)
			("^d" DCell)))
      (store-match-data data))))

(defun double-type (var)
  (memq (type var) '(UDCell DCell)))

(defun float-type (var)
  (equal (type var) 'Float))

(defun loads (vars)
  "C code for loading vars from the stack"
  (cond ((null vars) "")
	((double-type (car vars)) (format "{Double_Store _d; _d.cells.low = %s; _d.cells.high = %s; %s = _d.dcell;}\n%s"
					  (stack (+ 1 (cells (cdr vars))))
					  (stack (cells (cdr vars)))
					  (car vars)
					  (loads (cdr vars))))
	((float-type (car vars)) (format "%s = %s;\n%s"
					 (car vars)
					 (fstack (floats (cdr vars)))
					 (loads (cdr vars))))
	(t (format "%s = (%s) %s;\n%s"
		   (car vars)
		   (type (car vars))
		   (stack (cells (cdr vars)))
		   (loads (cdr vars))))))

(defun stores (vars effects)
  "C code for storing vars on the stack"
  (cond ((null vars) "")
	((redundantq vars effects) (format "/* store redundant */\n%s"
					   (stores (cdr vars) effects)))
	((double-type (car vars)) (format "{Double_Store _d; _d.dcell = %s; %s = _d.cells.low; %s = _d.cells.high;}\n%s"
					  (car vars)
					  (stack (+ 1 (cells (cdr vars))))
					  (stack (cells (cdr vars)))
					  (stores (cdr vars) effects)))
	((float-type (car vars)) (format "%s = %s;\n%s"
					 (fstack (floats (cdr vars)))
					 (car vars)
					 (stores (cdr vars) effects)))
	(t (format "%s = (Cell)%s ;\n%s"
		   (stack (cells (cdr vars)))
		   (car vars)
		   (stores (cdr vars) effects)))))

(defun redundantq (vars effects)
  "Is the store of (car vars) redundant?"
  (let ((in-vars (memq (car vars) (effect-in effects))))
    (and in-vars
	 (cond ((float-type (car vars)) (= (effect-floats effects)
					   (- (floats in-vars) (floats vars))))
	       (t (= (effect-cells effects)
		     (- (cells in-vars) (cells vars))))))))

(defun load-tos (effects)
  "TOS-loading code, if necessary"
  (format "%s%s"
	  (cond ((and (= 0 (cells (effect-out effects)))
		      (< 0 (cells (effect-in effects))))
		 "IF_TOS(TOS = sp[0]);\n")
		(t ""))
	  (cond ((and (= 0 (floats (effect-out effects)))
		      (< 0 (floats (effect-in effects))))
		 "IF_FTOS(FTOS = fp[0]);\n")
		(t ""))))

(defun store-tos (effects)
  "TOS-storing code, if necessary"
  (format "%s%s"
	  (cond ((or (redundant-tos effects)
		     (and (= 0 (cells (effect-in effects)))
			  (< 0 (cells (effect-out effects)))))
		 "IF_TOS(sp[0] = TOS);\n")
		(t ""))
	  (cond ((or (redundant-ftos effects)
		    (and (= 0 (floats (effect-in effects)))
			 (< 0 (floats (effect-out effects)))))
		    "IF_FTOS(fp[0] = FTOS);\n")
		(t ""))))

(defun redundant-tos (effects)
  "Does redundantq consider storing into the original TOS location redundant?"
  (red-tos1 (effect-out effects) effects))

(defun red-tos1 (vars effects)
  (cond ((null vars) nil)
	((and (not (float-type (car vars)))
	      (redundantq vars effects))
	 (or (= (- (effect-cells effects))
		(cells (cdr vars)))
	     (red-tos1 (cdr vars) effects)))))

(defun redundant-ftos (effects)
  "Does redundantq consider storing into the original FTOS location redundant?"
  (red-ftos1 (effect-out effects) effects))

(defun red-ftos1 (vars effects)
  (cond ((null vars) nil)
	((and (float-type (car vars))
	      (redundantq vars effects))
	 (or (= (- (effect-floats effects))
		(floats (cdr vars)))
	     (red-ftos1 (cdr vars) effects)))))

(defun stack (n)
  "the stack entry at depth n"
  (cond ((= n 0) "TOS")
	(t (format "sp[%d]" n))))

(defun fstack (n)
  "the float stack entry at depth n"
  (cond ((= n 0) "FTOS")
	(t (format "fp[%d]" n))))

(defun parse-stack-effect (stack-effect)
  "lists of items before and after --"
  (let ((effect-list (read (format "(%s)" stack-effect))))
    (let ((in (stack-before effect-list))
	  (out (stack-after effect-list)))
      (list in
	    out 
	    (- (cells in) (cells out))
	    (- (floats in) (floats out))))))

(defun effect-in (effects)
  (car effects))

(defun effect-out (effects)
  (cadr effects))

(defun effect-cells (effects)
  "the number of input - output cells" 
  (cadr (cdr effects)))

(defun effect-floats (effects)
  (cadr (cddr effects)))

(defun stack-before (effect-list)
  (cond ((equal (car effect-list) '--) nil)
	(t (cons (car effect-list) (stack-before (cdr effect-list))))))

(defun stack-after (effect-list)
  (cdr (memq '-- effect-list)))

(defun cadr (list)
  (car (cdr list)))

(defun caar (list)
  (car (car list)))

(defun cddr (list)
  (cdr (cdr list)))

(defun cadar (list)
  (car (cdr (car list))))

(defun make-c ()
  (filter-primitives "primitives.b" "primitives.i" 'c-filter))

(defun make-list ()
  (filter-primitives "primitives.b" "prim_labels.i" 'list-filter))

(defun make-alias ()
  (filter-primitives "primitives.b" "prim_alias.4th" 'alias-filter))

(defun make-forth ()
  (filter-primitives "primitives.b" "primitives.4th" 'forth-filter))
