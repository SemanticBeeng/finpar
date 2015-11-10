module Main where

import Control.DeepSeq
import Control.Applicative
import Control.Monad
import Data.Maybe
import System.CPUTime
import System.Environment

------------------------------------
--- Requires the installation of ---
--- Parsec package, i.e.,        ---
---     cabal install parsec     ---
------------------------------------

import Text.Parsec hiding (optional, (<|>), token)
import Text.Parsec.String

import Data.Bits
import Data.List 
import Prelude 

import Control.DeepSeq
import Data.Vector.Unboxed(Vector) 
import qualified Data.Vector.Unboxed as V

import Constants
import Date
import Genome
import EvalGenome
import MathMod

import Debug.Trace

------------------------------
--- Parser-related Helpers ---
------------------------------
signed :: Parser String -> Parser String
signed p = (char '-' >> (('-':) <$> p)) <|> p

whitespace :: Parser ()
whitespace = spaces <* optional (string "//" >> manyTill anyChar (char '\n') >> whitespace)

lexeme :: Parser a -> Parser a
lexeme p = p <* whitespace

token :: String -> Parser ()
token = void . lexeme . string

readInt :: Parser Int
readInt = lexeme $ read <$> signed (many1 digit)

readDouble :: Parser Double
readDouble = lexeme $ read <$> signed (s2 <|> s1)
  where s1 = do bef <- many1 digit
                aft <- fromMaybe "" <$> optional ((:) <$> char '.' <*> many1 digit)
                return $ bef ++ aft
        s2 = (++) <$> (char '.' >> pure "0.") <*> many1 digit

readArray :: Parser a -> Parser [a]
readArray p = lexeme $ between (token "[") (token "]") (p `sepBy` token ",")

---------------------------------------------
---------------------------------------------

findBestLik :: Int -> [Double] -> (Int, Double)
findBestLik pop_size log_liks =
    reduce (\ (best_ind, best_logLik) (ind, logLik) ->
                    if (logLik > best_logLik)
                    then (ind, logLik)
                    else (best_ind, best_logLik)
           ) (0, -100000000.0) (zip [0..pop_size-1] log_liks)

makeSummary ::     Vector Double -> Double -> Vector Double -> Vector Double
                -> (Genome, Double, [[Double]])
makeSummary win_genome win_logLik calib_prices black_prices =
    let res_genome = (  win_genome V.! 0, win_genome V.! 1, win_genome V.! 2, 
                        win_genome V.! 3, win_genome V.! 4      )
        res_summary= zipWith (\ price quote ->
                                    let err_ratio = abs $ 100.0 * (price - quote) / quote
                                    in  [10000.0*price, 10000.0*quote, err_ratio]
                             ) (V.toList calib_prices) (V.toList black_prices)
    in  (res_genome, win_logLik, res_summary)
    
---------------------------------------------
---- The Monte-Carlo Convergence Loop -------
---------------------------------------------

mcmcConvergenceLoop ::   Int -> Int -> [Swaption] -> Vector Int -> [Double] -> [Double]  -- loop read-only args
                      -> [Vector Double] -> [Vector Double] -> [Double] -> Int -> Int    -- loop variant   args
                      -> ( [Vector Double], [Vector Double], [Double], Int ) 
mcmcConvergenceLoop pop_size num_mcmc_its swaption_quotes sobol_dir_vct hermite_coeffs hermite_weights
                    genomes_old genomes_new logLiks_old j sob_offs0 =
    if j == num_mcmc_its
    then (genomes_old, genomes_new, logLiks_old, sob_offs0)
    else 
        -- select the way in which genomes are perturbed
    let move_selected = sobolNum sobol_dir_vct sob_offs0
        sob_offs1     = sob_offs0 + 1
        move_type     = selectMoveType move_selected

        -- perturb the genome population based on the selection
        (sob_offs2, genomes_new', bf_rats') = 
            case move_type of
                DIMS_ALL -> let sob_offs_new = sob_offs1 + pop_size*5
                                rand_vcts = map (\ i -> V.fromList $ map (sobolNum sobol_dir_vct) 
                                                                         [5*i+sob_offs1..5*i+sob_offs1+4]
                                                ) [0..pop_size-1]
                                (g, bfrat) = unzip $ zipWith3 mutateDimsALL 
                                                              genomes_old genomes_new rand_vcts
                            in  sob_offs_new `deepseq` g `deepseq` bfrat `deepseq` 
                                (sob_offs_new, g, bfrat)

                DIMS_ONE -> let sob_offs_new = sob_offs1 + pop_size*5 + 1 -- *1 
                                dim_j = truncate $ (fromIntegral genome_dim) * (sobolNum sobol_dir_vct sob_offs1)
--                                rand_nums = map (sobolNum sobol_dir_vct) 
--                                                [sob_offs1+1..sob_offs1+pop_size]
                                rand_vcts = map (\ i -> V.fromList $ map (sobolNum sobol_dir_vct) 
                                                                         [5*i+sob_offs1+1..5*i+sob_offs1+5]
                                                ) [0..pop_size-1]
                                -- create sobol numbers
                                (g, bfrat) = unzip $ zipWith3 (mutateDimsONE dim_j) 
                                                              genomes_old genomes_new rand_vcts -- rand_nums
                            in  sob_offs_new `deepseq` g `deepseq` bfrat `deepseq` 
                                (sob_offs_new, g, bfrat)

                DEMCMC   -> let sob_offs_new = sob_offs1 + pop_size*8
                                rand_vcts = map (\ i -> V.fromList $ map (sobolNum sobol_dir_vct) 
                                                                         [8*i+sob_offs1..8*i+sob_offs1+7]
                                                ) [0..pop_size-1]
                                (g, bfrat) = unzip $ zipWith (mcmcDE pop_size genomes_old) 
                                                             rand_vcts [0..pop_size-1]

                            in  sob_offs_new `deepseq` g `deepseq` bfrat `deepseq` 
                                (sob_offs_new, g, bfrat)
                    
        -- evaluate the new genome population
        --(logLiks_new', _, _) = unzip3 $ map (evalGenome hermite_coeffs hermite_weights swaption_quotes) genomes_new'
        (logLiks_new_th, sw_pr_th, bl_pr_th) = 
                unzip3 $ map (evalGenome hermite_coeffs hermite_weights swaption_quotes) genomes_new'
        logLiks_new' = logLiks_new_th `deepseq` sw_pr_th `deepseq` bl_pr_th `deepseq` 
                       logLiks_new_th

        -- decide which genomes to ACCEPT/update
        rand_nums = map (sobolNum sobol_dir_vct) [sob_offs2..sob_offs2+pop_size-1]
        sob_offs3 = sob_offs2 + pop_size
        (genomes_old', logLiks_old') = unzip $
            zipWith6 (\ genome_old genome_new logLik_old logLik_new bf_rat r01 ->
                          let exp_term   = logLik_new - logLik_old
                              acceptance = min 1.0 $ (exp exp_term) * bf_rat 
                          in  if r01 < acceptance
                              then (genome_new, logLik_new)
                              else (genome_old, logLik_old)
                     ) genomes_old genomes_new' logLiks_old logLiks_new' bf_rats' rand_nums
{-
        sob_offs3' =  if (j `mod` 16) == 0
                      then let (w_ind, w_lik) = findBestLik pop_size logLiks_old'
                           in  trace  ("It: "++show j++" Best Lik: "++show w_lik++" Genome Index: "++show w_ind) 
                               sob_offs3
                      else sob_offs3
-}
    in  sob_offs3 `deepseq` genomes_old' `deepseq` genomes_new' `deepseq` logLiks_old' `deepseq`
        mcmcConvergenceLoop pop_size num_mcmc_its swaption_quotes sobol_dir_vct hermite_coeffs hermite_weights
                            genomes_old' genomes_new' logLiks_old' (j+1) sob_offs3
    

------------------------------------------------
--- Main Entry Point: volatility calibration ---
------------------------------------------------
-- genome == { g_a, g_b, g_rho, g_nu, g_sigma }
compute ::   Int -> Int -> [Swaption]  -> [Double]   -> [Double] -> Vector Int 
          -> (Genome,Double,[[Double]]) 
compute pop_size num_mcmc_its swaption_quotes 
        hermite_coeffs hermite_weights sobol_dir_vct =

    let ----------------------------------------------------
        -- initialization stage: compute POP_SIZE genomes --
        ----------------------------------------------------
        sobol_off0= 1
        -- compute 5*POP_SIZE random numbers!
        rand_vcts = map (\ i -> V.fromList $ map (sobolNum sobol_dir_vct) 
                                                 [5*i+sobol_off0..5*i+sobol_off0+4]
                        ) [0..pop_size-1]
        -- initalize the genomes!
        genomes   = map (\ r01_vct5 -> 
                            V.zipWith3  (\ g_max g_min r01 -> (g_max - g_min)*r01 + g_min )
                                        g_maxs g_mins r01_vct5
                        ) rand_vcts 
        sobol_off1= sobol_off0 + pop_size*5

        -- evaluate the just initalized genomes!
        (logLiks_th,sw_pth,bl_pth)= unzip3 $ map (evalGenome hermite_coeffs hermite_weights swaption_quotes) genomes
        (logLiks,_,_)= deepseq (logLiks_th,sw_pth,bl_pth) (logLiks_th,sw_pth,bl_pth)

        -- run the MCMC Convergence Loop
        (genomes', genomes_prop', logLiks', sobol_off2) = 
                mcmcConvergenceLoop pop_size num_mcmc_its 
                                    swaption_quotes sobol_dir_vct 
                                    hermite_coeffs hermite_weights
                                    genomes genomes logLiks 0 sobol_off1

        -- find the best genome and recompute 
        --      the calibrated and reference price for it 
        (win_ind, win_logLik) = findBestLik pop_size logLiks'
        win_genome            = genomes' !! win_ind
        (_, calib_prices, black_prices) = 
                evalGenome hermite_coeffs hermite_weights swaption_quotes win_genome

        -- format the result for printing 
        (res_genome, res_logLik, res_swap_arr) = makeSummary win_genome win_logLik calib_prices black_prices
        
    in  (res_genome, res_logLik, res_swap_arr)

-----------------------------------------
--- Entry point for Generic Pricing   ---
--- The only place where using Monads,---
--- e.g., parsing Dataset from StdIn  ---
-----------------------------------------
main :: IO ()
main = do s <- getContents 
          case parse run "input" s of
            Left  e        -> error $ show e
            Right m -> do
              (v, runtime) <- m
              result <- getEnv "HIPERMARK_RESULT"
              writeFile result $ jsonish v
              runtime_file <- getEnv "HIPERMARK_RUNTIME"
              writeFile runtime_file $ show runtime
  where run = do whitespace
                 pop_size        <- readInt
                 num_mcmc_its    <- readInt
                 num_swap_quotes <- readInt
                 swaption_quotes <- readDouble2d
                 num_hermite     <- readInt
                 hermite_coeffs  <- readDouble1d
                 hermite_weights <- readDouble1d
                 num_sobol_bits  <- readInt
                 sobol_dir_vct   <- readInt1d

--                 let num_swap_quotes =  testSwaptionPricer hermite_coeffs hermite_weights $ 
--                                        testG2ppUtil $ testMathMod $ test_dates num_swap_quotes0

                 let swaption_quotes'= map (\x -> (x!!0, x!!1, x!!2, x!!3)) swaption_quotes

                 let (win_genome, win_logLik, win_swap_arr) =
                        compute pop_size num_mcmc_its swaption_quotes'
                                hermite_coeffs hermite_weights (V.fromList sobol_dir_vct)

                 return $ do
                   start <- getCPUTime -- In picoseconds; 1 microsecond == 10^6 picoseconds.
                   let v = (win_genome, win_logLik, win_swap_arr)
                   end <- v `deepseq` getCPUTime
                   return (v, (end - start) `div` 1000000)

        readInt1d    = readArray readInt
        readDouble1d = readArray readDouble
        readDouble2d = readArray $ readArray readDouble

jsonish :: (Genome, Double, [[Double]])
        -> String
jsonish ((win_a, win_b, win_rho, win_nu, win_sigma), win_logLik, win_swap_arr) =
  unlines [ "{"
          , intercalate ",\n"
            [ field "a_field" win_a
            , field "b_field" win_b
            , field "sigma_field" win_sigma
            , field "nu_field" win_nu
            , field "rho_field" win_rho
            , field "lg_likelyhood" win_logLik
            , field "swaption_calibration_result" win_swap_arr
            ]
          , "}"
          ]
  where field k v = "\"" ++ k ++ "\": " ++ show v

-- ghc -O2 -msse2 -rtsopts  PricingLexiFi.hs
-- ./PricingLexiFi +RTS -K128m -RTS < ../Data/Medium/input.data
